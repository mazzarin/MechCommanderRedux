#include "stdafx.h"
#include "gui/MCGuiComboBox.h"
#include "gui/MCGuiButton.h"
#include "gui/MCGuiListBox.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCGuiTextObject.h"

auto CloseListOnMousedown(MCGuiObject& obj, MCGuiEvent& event) -> void
{
    if (event.Type == MCGuiEventType::LeftButtonDown)
    {
        if (const std::shared_ptr<MCGuiObject> parent = obj.ParentObject(); parent != nullptr)
        {
            APostMessage(*parent, MCGuiComboBox::ToggleList);
        }
    }
}

MCGuiComboBox::~MCGuiComboBox()
{
    Destroy();
}

auto MCGuiComboBox::Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name) -> int32_t
{
    if (const int32_t result = MCGuiObject::Init(xPos, yPos, width, height, name); result != 0)
    {
        return result;
    }

    // The original uses each new part unchecked.
    ListBox = CreateChild<MCGuiListBox>(
        [&](MCGuiListBox& made)
        {
            made.Init(10, 10, width, height, {});
            made.ShowGuiWindow(false);
        });
    ListBox->MoveTo(2, height + 2);
    ListBox->SetEventRoutine(CloseListOnMousedown);

    // The drop button is sized (height, width): its art sets the real size.
    DropButton = CreateChild<MCGuiToolButton>(
        [&](MCGuiToolButton& made)
        {
            made.Init(10, 10, height, width, {});
            made.SetUpPicture(0xe);
            made.SetDownPicture(0xf);
            made.Callback().SetMessage(weak_from_this(), ToggleList);
        });
    const int32_t buttonWidth = DropButton->Width();
    DropButton->MoveTo(Width() - (buttonWidth + 2), 2);

    TextField = CreateChild<MCGuiTextObject>([&](MCGuiTextObject& made)
                                             { made.Init(10, 10, (width - (buttonWidth + 2)) - 2, height - 4, {}); });
    TextField->MoveTo(2, 2);
    TextField->ReadOnly = true;
    SetBackColor(3);
    return 0;
}

auto MCGuiComboBox::HandleEvent(MCGuiEvent& event) -> void
{
    if (event.Type != ToggleList)
    {
        return;
    }

    if (ListBox->IsShowing())
    {
        // Close the list and take its selection.
        ListBox->ShowGuiWindow(false);
        const std::optional<std::string> selected = ListBox->GetItemString(ListBox->SelectedItem);
        TextField->SetText(selected.value_or(std::string()));
        DropButton->Pushed = false;
        return;
    }

    ListBox->ShowGuiWindow(true);
    DropButton->Pushed = true;
}

auto MCGuiComboBox::Draw() -> void
{
    DrawFramed(false, true);
    MCGuiObject::Draw();
}

auto MCGuiComboBox::Resize(int32_t newWidth, int32_t newHeight) -> void
{
    MCGuiObject::Resize(newWidth, newHeight);
    ListBox->Resize(newWidth, ListBox->Height());
    ListBox->MoveTo(2, newHeight + 2);
    const int32_t buttonX = newWidth - (DropButton->Width() + 2);
    DropButton->MoveTo(buttonX, 2);
    TextField->Resize(buttonX - 2, newHeight - 4);
}

auto MCGuiComboBox::ChangeItemString(int32_t item, std::string_view text) const -> bool
{
    if (!ListBox->ChangeItemString(item, text))
    {
        return false;
    }

    if (item == ListBox->SelectedItem)
    {
        TextField->SetText(text);
    }

    return true;
}

auto MCGuiComboBox::SelectItem(int32_t item) const -> bool
{
    if (!ListBox->SelectItem(item))
    {
        return false;
    }

    const std::optional<std::string> selected = ListBox->GetItemString(item);
    TextField->SetText(selected.value_or(std::string()));
    return true;
}
