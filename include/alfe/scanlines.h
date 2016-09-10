#include "alfe/main.h"
#include "alfe/bitmap.h"
#include "alfe/complex.h"
#include "alfe/fft.h"

#ifndef INCLUDED_SCANLINES_H
#define INCLUDED_SCANLINES_H

#include "alfe/image_filter.h"

float sinint(float x)
{
    float mr = 1;
    if (x < 0) {
        x = -x;
        mr = -1;
    }
    if (x < 10) {
        float i = 3;
        float r = x;
        float x2 = -x*x;
        float t = x;
        static const float eps = 1.0f/(1 << 16);
        do {
            t *= x2/(i*(i - 1));
            r += t/i;
            i += 2;
        } while (t < -eps || t > eps);
        return r * mr;
    }
    float cr = 1;
    float sr = 1/x;
    float ct = 1;
    float st = 1/x;
    float i = 2;
    float x2 = -1/(x*x);
    do {
        float lct = ct;
        ct *= x2*i*(i - 1);
        st *= x2*i*(i + 1);
        if (abs(ct) > abs(lct) || ct == 0)
            break;
        cr += ct;
        sr += st;
        i += 2;
    } while (true);
    return (static_cast<float>(tau)/4.0f - (cos(x)/x)*cr - (sin(x)/x)*sr) * mr;
}

class ScanlineRenderer
{
public:
    ScanlineRenderer()
      : _profile(4), _horizontalProfile(4), _width(1), _bleeding(2),
        _horizontalBleeding(2), _horizontalRollOff(0), _verticalRollOff(0),
        _subPixelSeparation(0), _phosphor(0), _mask(0), _maskSize(0)
    { }
    void init()
    {
        float kernelRadiusVertical = 16;
        _output.ensure(_size.x*3*sizeof(float), _size.y);

        Timer timerVerticalGenerate;
        _vertical.generate(_size, 3, kernelRadiusVertical,
            kernel(_profile, _zoom.y, _width, _verticalRollOff), &_inputTL.y,
            &_inputBR.y, _zoom.y, _offset.y);
        timerVerticalGenerate.output("vertical generate");

        int inputHeight = _inputBR.y - _inputTL.y;
        _intermediate.ensure(_size.x*3*sizeof(float), inputHeight);

        _vertical.setBuffers(_intermediate, _output);

        static const float inputChannelPositions[3] = {0, 0, 0};
        float outputChannelPositions[3] =
            {-_subPixelSeparation/3, 0, _subPixelSeparation/3};

        float kernelRadiusHorizontal = 16;
        Timer timerHorizontalGenerate;
        auto channelKernel = kernel(_horizontalProfile, _zoom.x, 1,
            _horizontalRollOff);
        _horizontal.generate(Vector(_size.x, inputHeight), 3,
            inputChannelPositions, 3, outputChannelPositions,
            kernelRadiusHorizontal,
            [=](float distance, int inputChannel, int outputChannel)
            {
                if (inputChannel != outputChannel)
                    return 0.0f;
                return channelKernel(distance);
            },
            &_inputTL.x, &_inputBR.x, _zoom.x, _offset.x);
        timerHorizontalGenerate.output("horizontal generate");

        _input.ensure((_inputBR.x - _inputTL.x)*3*sizeof(float), inputHeight);

        _horizontal.setBuffers(_input, _intermediate);
    }
    void render()
    {
        Timer timerHorizontalExecute;
        _horizontal.execute();
        timerHorizontalExecute.output("horizontal execute");
        Timer timerHorizontalBleeding;
        Byte* d = _intermediate.data();
        for (int y = 0; y < _inputBR.y - _inputTL.y; ++y) {
            bleed(d, 12, Vector(3, _size.x), _horizontalBleeding);
            d += _intermediate.stride();
        }
        timerHorizontalBleeding.output("horizontal bleeding");
        Timer timerVerticalExecute;
        _vertical.execute();
        timerVerticalExecute.output("vertical execute");
        Timer timerBleeding;
        bleed(_output.data(), _output.stride(), _size*Vector(3, 1), _bleeding);
        timerBleeding.output("bleeding");
    }
    int getProfile() { return _profile; }
    void setProfile(int profile) { _profile = profile; }
    float getWidth() { return _width; }
    void setWidth(float width) { _width = width; }
    int getBleeding() { return _bleeding; }
    void setBleeding(int bleeding) { _bleeding = bleeding; }
    int getHorizontalProfile() { return _horizontalProfile; }
    void setHorizontalProfile(int profile) { _horizontalProfile = profile; }
    int getHorizontalBleeding() { return _horizontalBleeding; }
    void setHorizontalBleeding(int bleeding)
    {
        _horizontalBleeding = bleeding;
    }
    float getVerticalRollOff() { return _verticalRollOff; }
    void setVerticalRollOff(float rollOff) { _verticalRollOff = rollOff; }
    float getHorizontalRollOff() { return _horizontalRollOff; }
    void setHorizontalRollOff(float rollOff) { _horizontalRollOff = rollOff; }
    float getSubPixelSeparation() { return _subPixelSeparation; }
    void setSubPixelSeparation(float separation)
    {
        _subPixelSeparation = separation;
    }
    int getPhosphor() { return _phosphor; }
    void setPhosphor(int phosphor) { _phosphor = phosphor; }
    int getMask() { return _mask; }
    void setMask(int mask) { _mask = mask; }
    float getMaskSize() { return _maskSize; }
    void setMaskSize(float maskSize) { _maskSize = maskSize; }
    void setZoom(Vector2<float> zoom) { _zoom = zoom; }
    void setOffset(Vector2<float> offset) { _offset = offset; }
    void setOutputSize(Vector size) { _size = size; }
    Vector inputTL() { return _inputTL; }
    Vector inputBR() { return _inputBR; }
    AlignedBuffer input() { return _input; }
    AlignedBuffer output() { return _output; }

private:
    std::function<float(float)> kernel(int profile, float zoom, float width,
        float rollOff)
    {
        switch (profile) {
            case 0:
                // Rectangle
                {
                    float a = 2.0f/(static_cast<float>(tau)*width);
                    float b = 0.5f*zoom*static_cast<float>(tau)*width/2.0f;
                    float c = 0.5f*zoom*static_cast<float>(tau);
                    return [=](float d)
                    {
                        return a*(sinint(b + c*d) + sinint(b - c*d))*
                            sinc(d*rollOff);
                    };
                }
                break;
            case 1:
                // Triangle
                return [=](float distance)
                {
                    float b = 0.5f*zoom;
                    float w = 0.5f*width;
                    float f = distance;
                    float t = static_cast<float>(tau);
                    return (
                        +2*t*(f-w)*b*sinint(t*(f-w)*b)
                        +2*t*(f+w)*b*sinint(t*(f+w)*b)
                        -4*t*f*b*sinint(t*f*b)
                        +2*cos(b*t*(f-w))
                        +2*cos(b*t*(f+w))
                        -4*cos(b*t*f)
                        )*sinc(distance*rollOff)/(t*t*w*w*b);
                };
                break;
            case 2:
                // Circle
                {
                    float b = 4.0f/(width*width);
                    float a = 2.0f*b*width/static_cast<float>(tau);
                    float c = width/2.0f;
                    return [=](float distance)
                    {
                        if (distance < -c || distance > c)
                            return 0.0f;
                        return a*sqrt(1 - b*distance*distance)*
                            sinc(distance*rollOff);
                    };
                }
                break;
            case 3:
                // Gaussian
                {
                    float a = -8/(width*width);
                    float b = 4/(sqrt(static_cast<float>(tau))*width);
                    return [=](float distance)
                    {
                        return b*exp(a*distance*distance)*
                            sinc(distance*rollOff);
                    };
                }
                break;
            case 4:
                // Sinc
                {
                    float bandLimit = min(1.0f, zoom)/width;
                    if (width == 0)
                        bandLimit = 10000;
                    return [=](float distance)
                    {
                        return bandLimit*sinc(distance*bandLimit)*
                            sinc(distance*rollOff);
                    };
                }
                break;
            default:
                // Box
                {
                    return [=](float distance)
                    {
                        return (distance >= -0.5f && distance < 0.5f) ?
                            sinc(distance*rollOff) : 0.0f;
                    };
                }
                break;
        }
    }
    void bleed(Byte* data, int s, Vector size, int bleeding)
    {
        if (bleeding == 1) {
            Byte* outputColumn = data;
            for (int x = 0; x < size.x; ++x) {
                Byte* output = outputColumn;
                float bleed = 0;
                for (int y = 0; y < size.y; ++y) {
                    float o = bleed + *reinterpret_cast<float*>(output);
                    bleed = 0;
                    if (o < 0) {
                        bleed = o;
                        o = 0;
                    }
                    if (o > 1) {
                        bleed = o - 1;
                        o = 1;
                    }
                    *reinterpret_cast<float*>(output) = o;
                    output += s;
                }
                outputColumn += sizeof(float);
            }
            return;
        }
        if (bleeding == 2) {
            Byte* outputColumn = data;
            for (int x = 0; x < size.x; ++x) {
                Byte* output = outputColumn;
                float bleed = 0;
                for (int y = 0; y < size.y; ++y) {
                    float o = *reinterpret_cast<float*>(output);
                    if (o < 0) {
                        Byte* output1 = output + s;
                        int y1;
                        float t = -o;
                        *reinterpret_cast<float*>(output) = 0;
                        for (y1 = y + 1; y1 < size.y; ++y1, output1 += s) {
                            float o1 = *reinterpret_cast<float*>(output1);
                            if (o1 > 0)
                                break;
                            t -= o1;
                            *reinterpret_cast<float*>(output1) = 0;
                        }
                        float bleed = t/2;
                        Byte* output2 = output - s;
                        for (int y2 = y - 1; y2 >= 0; --y2, output2 -= s) {
                            float* p = reinterpret_cast<float*>(output2);
                            float o2 = *p;
                            if (o2 > 0) {
                                if (bleed > o2) {
                                    bleed -= o2;
                                    *p = 0;
                                }
                                else {
                                    *p = o2 - bleed;
                                    break;
                                }
                            }
                        }
                        bleed = t/2;
                        output2 = output1;
                        for (int y2 = y1; y2 < size.y; ++y2, output2 += s) {
                            float* p = reinterpret_cast<float*>(output2);
                            float o2 = *p;
                            if (o2 > 0) {
                                if (bleed > o2) {
                                    bleed -= o2;
                                    *p = 0;
                                }
                                else {
                                    *p = o2 - bleed;
                                    break;
                                }
                            }
                        }
                        // We need to restart at y1 here rather than y2 because
                        // there might be some more <0 values between y1 and
                        // y2.
                        y = y1 - 1;
                        output = output1;
                        continue;
                    }
                    if (o > 1) {
                        Byte* output1 = output + s;
                        int y1;
                        float t = o - 1;
                        *reinterpret_cast<float*>(output) = 1;
                        for (y1 = y + 1; y1 < size.y; ++y1, output1 += s) {
                            float o1 = *reinterpret_cast<float*>(output1);
                            if (o1 < 1)
                                break;
                            t += o1 - 1;
                            *reinterpret_cast<float*>(output1) = 1;
                        }
                        float bleed = t/2;
                        Byte* output2 = output - s;
                        for (int y2 = y - 1; y2 >= 0; --y2, output2 -= s) {
                            float* p = reinterpret_cast<float*>(output2);
                            float o2 = *p;
                            if (o2 < 1) {
                                if (bleed + o2 > 1) {
                                    bleed -= 1 - o2;
                                    *p = 1;
                                }
                                else {
                                    *p = o2 + bleed;
                                    break;
                                }
                            }
                        }
                        bleed = t/2;
                        output2 = output1;
                        for (int y2 = y1; y2 < size.y; ++y2, output2 += s) {
                            float* p = reinterpret_cast<float*>(output2);
                            float o2 = *p;
                            if (o2 < 1) {
                                if (bleed + o2 > 1) {
                                    bleed -= 1 - o2;
                                    *p = 1;
                                }
                                else {
                                    *p = o2 + bleed;
                                    break;
                                }
                            }
                        }
                        // We need to restart at y1 here rather than y2 because
                        // there might be some more >1 values between y1 and
                        // y2.
                        y = y1 - 1;
                        output = output1;
                        continue;
                    }
                    output += s;
                }
                outputColumn += sizeof(float);
            }
        }
    }

    int _profile;
    int _horizontalProfile;
    float _width;
    int _bleeding;
    int _horizontalBleeding;
    float _horizontalRollOff;
    float _verticalRollOff;
    float _subPixelSeparation;
    int _phosphor;
    int _mask;
    float _maskSize;
    Vector2<float> _offset;
    Vector2<float> _zoom;
    Vector _size;
    AlignedBuffer _input;
    AlignedBuffer _intermediate;
    AlignedBuffer _output;
    Vector _inputTL;
    Vector _inputBR;

    ImageFilterHorizontal _horizontal;
    ImageFilterVertical _vertical;
};

#endif // INCLUDED_SCANLINES_H
