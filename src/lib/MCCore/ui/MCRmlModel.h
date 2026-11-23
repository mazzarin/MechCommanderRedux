#pragma once

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>

/// <summary>
/// Port-only (SPEC-menus.md, "Models and views"): the one way a remade screen shows a model, as RmlUi's data bindings:
/// the model's values and lists at the binding's top level (RmlUi sees a change only there), each named for the
/// screen's document (<c>{{status}}</c>, <c>data-for="game : games"</c>, <c>data-value="volume"</c>), and its actions
/// (<c>data-event-click="pick(i)"</c>). The view never holds the game's state: the screen copies its model's values into
/// what is bound here and says what changed (<see cref="Changed"/>).
/// </summary>
/// <remarks>
/// Made before the screen's document is read (MCRmlScreen::MakeModels), so the document binds to it; what is bound
/// outlives it (the screen's members). Its destructor takes it out of its context.
/// </remarks>
class MCRmlModel
{
public:
    /// <summary>A model named <paramref name="name"/> in <paramref name="context"/> (none when the name is taken).</summary>
    MCRmlModel(Rml::Context& context, std::string name)
        : _Context(context), _Name(std::move(name)), _Constructor(context.CreateDataModel(_Name))
    {
        if (_Constructor)
        {
            _Handle = _Constructor.GetModelHandle();
            _Made = true;
        }
    }

    ~MCRmlModel()
    {
        if (_Made)
        {
            _Context.RemoveDataModel(_Name);
        }
    }

    MCRmlModel(const MCRmlModel&) = delete;
    MCRmlModel& operator=(const MCRmlModel&) = delete;

    /// <summary>Whether the model was made (its name not taken).</summary>
    explicit operator bool() const { return _Made; }

    /// <summary>The value <paramref name="value"/> bound as <paramref name="name"/> (a number, a word, a flag).</summary>
    template <typename T> bool Value(const std::string& name, T& value)
    {
        return _Constructor && _Constructor.Bind(name, &value);
    }

    /// <summary>
    /// The list <paramref name="rows"/> bound as <paramref name="name"/>, each row's <paramref name="fields"/> named for
    /// the document (<c>{"name", &amp;MCGame::Name}</c>).
    /// </summary>
    template <typename Row, typename... Fields>
    bool List(const std::string& name, std::vector<Row>& rows, std::pair<const char*, Fields Row::*>... fields)
    {
        if (!_Constructor)
        {
            return false;
        }

        if (Rml::StructHandle<Row> row = _Constructor.RegisterStruct<Row>(); row)
        {
            (row.RegisterMember(fields.first, fields.second), ...);
        }

        return _Constructor.RegisterArray<std::vector<Row>>() && _Constructor.Bind(name, &rows);
    }

    /// <summary>The action <paramref name="name"/>, called with the document's arguments (a row's index, say).</summary>
    bool Event(const std::string& name, std::function<void(const Rml::VariantList& arguments)> act)
    {
        return _Constructor && _Constructor.BindEventCallback(
                                   name, [act = std::move(act)](Rml::DataModelHandle, Rml::Event&,
                                                                const Rml::VariantList& arguments) { act(arguments); });
    }

    /// <summary>What is bound as <paramref name="name"/> changed: its views are made again on the next frame.</summary>
    void Changed(const std::string& name)
    {
        if (_Handle)
        {
            _Handle.DirtyVariable(name);
        }
    }

    /// <summary>Everything bound changed.</summary>
    void Changed()
    {
        if (_Handle)
        {
            _Handle.DirtyAllVariables();
        }
    }

private:
    Rml::Context& _Context;
    std::string _Name;
    Rml::DataModelConstructor _Constructor;
    Rml::DataModelHandle _Handle;
    bool _Made = false;
};
