class NMISwitch : public ISA8BitComponent<NMISwitch>
{
public:
    NMISwitch() : _connector(this) { }
    void write(UInt8 data) { _nmiOn = ((data & 0x80) != 0); }
    String save() const
    {
        return String("{ on: ") + String::Boolean(_nmiOn) + ", active: " +
            String::Boolean(_active) + " }\n";
    }
    ::Type persistenceType() const
    {
        List<StructuredType::Member> members;
        members.add(StructuredType::Member("on", false));
        members.add(StructuredType::Member("active", false));
        return StructuredType("NMISwitch", members);
    }
    void load(const Value& value)
    {
        auto members = value.value<HashTable<Identifier, Value>>();
        _nmiOn = members["on"].value<bool>();
        _active = members["active"].value<bool>();
    }
    class Connector : public OutputConnector<bool>
    {
    public:
        Connector(NMISwitch* component) : _component(component) { }
        //void connect(::Connector* other)
        //{
        //    // TODO
        //}
    protected:
        ::Connector::Type type() const { return Type(); }

    private:
        NMISwitch* _component;
    };
    static String name() { return "NMISwitch"; }

private:
    bool _nmiOn;
    Connector _connector;
};
