#include "clControlWithItems.h"
#include "clTreeCtrl.h"
#include <cmath>
#include <wx/minifram.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>

#if defined(__WXGTK__) || defined(__WXOSX__)
#define USE_PANEL_PARENT 1
#else
#define USE_PANEL_PARENT 0
#endif

#if USE_PANEL_PARENT
#include <wx/panel.h>
#endif

wxDEFINE_EVENT(wxEVT_TREE_SEARCH_TEXT, wxTreeEvent);
wxDEFINE_EVENT(wxEVT_TREE_CLEAR_SEARCH, wxTreeEvent);

//===------------------------
// Helper class
//===------------------------
class clSearchControl :
#if USE_PANEL_PARENT
    public wxPanel
#else
    public wxMiniFrame
#endif
{
    wxTextCtrl* m_textCtrl = nullptr;

private:
    void DoSelect(bool next)
    {
        clTreeCtrl* tree = dynamic_cast<clTreeCtrl*>(GetParent());
        if(!tree || m_textCtrl->IsEmpty()) { return; }
        wxTreeItemId where = next ? tree->FindNext(tree->GetSelection(), m_textCtrl->GetValue(), 0,
                                                   wxTR_SEARCH_DEFAULT & ~wxTR_SEARCH_INCLUDE_CURRENT_ITEM)
                                  : tree->FindPrev(tree->GetSelection(), m_textCtrl->GetValue(), 0,
                                                   wxTR_SEARCH_DEFAULT & ~wxTR_SEARCH_INCLUDE_CURRENT_ITEM);
        if(where.IsOk()) {
            clRowEntry* row = reinterpret_cast<clRowEntry*>(where.GetID());
            clMatchResult res = row->GetHighlightInfo();

            // This will remove all the matched info, including the last call to FindNext/Prev
            tree->ClearAllHighlights();

            // Set back the match info
            row->SetHighlightInfo(res);

            // Select the item
            tree->SelectItem(where);

            // Make sure its visible
            tree->EnsureVisible(where);

            // Highlight the result
            tree->HighlightText(where, true);
        }
    }

public:
    clSearchControl(clControlWithItems* parent)
#if USE_PANEL_PARENT
        : wxPanel(parent)
#else
        : wxMiniFrame(parent, wxID_ANY, "Find", wxDefaultPosition, wxDefaultSize,
                      wxFRAME_FLOAT_ON_PARENT | wxBORDER_SIMPLE)
#endif
    {
        SetSizer(new wxBoxSizer(wxVERTICAL));
        wxPanel* mainPanel = new wxPanel(this);
        GetSizer()->Add(mainPanel, 1, wxEXPAND);
        mainPanel->SetSizer(new wxBoxSizer(wxVERTICAL));
        int scrollBarWidth = wxSystemSettings::GetMetric(wxSYS_VSCROLL_X, parent);
        wxSize searchControlSize(GetParent()->GetSize().GetWidth() / 2 - scrollBarWidth, -1);
        m_textCtrl = new wxTextCtrl(mainPanel, wxID_ANY, "", wxDefaultPosition, searchControlSize,
                                    wxTE_RICH | wxTE_PROCESS_ENTER);
        mainPanel->GetSizer()->Add(m_textCtrl, 0, wxEXPAND);
        m_textCtrl->CallAfter(&wxTextCtrl::SetFocus);
        m_textCtrl->Bind(wxEVT_TEXT, &clSearchControl::OnTextUpdated, this);
        m_textCtrl->Bind(wxEVT_KEY_DOWN, &clSearchControl::OnKeyDown, this);
        GetSizer()->Fit(this);
        Hide();
    }

    virtual ~clSearchControl()
    {
        m_textCtrl->Unbind(wxEVT_TEXT, &clSearchControl::OnTextUpdated, this);
        m_textCtrl->Unbind(wxEVT_KEY_DOWN, &clSearchControl::OnKeyDown, this);

        // Let the parent know that we were dismissed
        clControlWithItems* parent = dynamic_cast<clControlWithItems*>(GetParent());
        parent->SearchControlDismissed();
    }

    void PositionControl()
    {
#if USE_PANEL_PARENT
        int x = GetParent()->GetSize().GetWidth() / 2;
        int y = GetParent()->GetSize().GetHeight() - GetSize().GetHeight();
        SetPosition(wxPoint(x, y));
#else
        wxPoint parentPt = GetParent()->GetScreenPosition();
        CenterOnParent();
        SetPosition(wxPoint(GetPosition().x, parentPt.y - m_textCtrl->GetSize().GetHeight()));
#endif
    }
    void DoSelectNone() { m_textCtrl->SelectNone(); }

    void InitSearch(const wxChar& ch)
    {
        m_textCtrl->SetFocus();
        m_textCtrl->ChangeValue(wxString() << ch);
        m_textCtrl->SetInsertionPointEnd();
        CallAfter(&clSearchControl::DoSelectNone);
    }

    void ShowControl(const wxChar& ch)
    {
        Show();
        m_textCtrl->ChangeValue("");
        PositionControl();
        CallAfter(&clSearchControl::InitSearch, ch);
    }

    void SelectNext() { DoSelect(true); }

    void SelectPrev() { DoSelect(false); }

    void Dismiss()
    {
        GetParent()->CallAfter(&wxWindow::SetFocus);
        // Clear the search
        wxTreeEvent e(wxEVT_TREE_CLEAR_SEARCH);
        e.SetEventObject(GetParent());
        GetParent()->GetEventHandler()->QueueEvent(e.Clone());
        Hide();
    }

    void OnTextUpdated(wxCommandEvent& event)
    {
        event.Skip();
        wxTreeEvent e(wxEVT_TREE_SEARCH_TEXT);
        e.SetString(m_textCtrl->GetValue());
        e.SetEventObject(GetParent());
        GetParent()->GetEventHandler()->QueueEvent(e.Clone());
    }

    void OnKeyDown(wxKeyEvent& event)
    {
        if(event.GetKeyCode() == WXK_ESCAPE) {
            Dismiss();
            return;
        } else if(event.GetKeyCode() == WXK_DOWN) {
            SelectNext();
        } else if(event.GetKeyCode() == WXK_UP) {
            SelectPrev();
        } else if(event.GetKeyCode() == WXK_RETURN || event.GetKeyCode() == WXK_NUMPAD_ENTER) {
            // Activate the item
            clTreeCtrl* tree = dynamic_cast<clTreeCtrl*>(GetParent());
            wxTreeEvent evt(wxEVT_TREE_ITEM_ACTIVATED);
            evt.SetEventObject(tree);
            evt.SetItem(tree->GetSelection());
            tree->GetEventHandler()->AddPendingEvent(evt);
            Dismiss();
        } else {
            event.Skip();
        }
    }
};

clControlWithItems::clControlWithItems(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size,
                                       long style)
    : clScrolledPanel(parent, id, pos, size, style)
    , m_viewHeader(this)
{
    DoInitialize();
}

clControlWithItems::clControlWithItems()
    : m_viewHeader(this)
{
}

bool clControlWithItems::Create(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
{
    if(!clScrolledPanel::Create(parent, id, pos, size, style)) { return false; }
    DoInitialize();
    return true;
}

void clControlWithItems::DoInitialize()
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_SIZE, &clControlWithItems::OnSize, this);
    Bind(wxEVT_MOUSEWHEEL, &clControlWithItems::OnMouseScroll, this);
    Bind(wxEVT_SET_FOCUS, [&](wxFocusEvent& e) {
        if(m_searchControl && m_searchControl->IsShown()) { m_searchControl->Dismiss(); }
    });
    wxSize textSize = GetTextSize("Tp");
    SetLineHeight(clRowEntry::Y_SPACER + textSize.GetHeight() + clRowEntry::Y_SPACER);
    SetIndent(0);
}

clControlWithItems::~clControlWithItems()
{
    m_searchControl = nullptr;
    Unbind(wxEVT_SIZE, &clControlWithItems::OnSize, this);
    Unbind(wxEVT_MOUSEWHEEL, &clControlWithItems::OnMouseScroll, this);
}

void clControlWithItems::SetHeader(const clHeaderBar& header)
{
    wxASSERT_MSG(IsEmpty(), "SetHeader can not be called on a non empty control");
    m_viewHeader = header;
    SetShowHeader(true);
}

void clControlWithItems::SetShowHeader(bool b)
{
    m_viewHeader.SetHideHeaders(!b);
    Refresh();
}

bool clControlWithItems::IsHeaderVisible() const { return !m_viewHeader.IsHideHeaders(); }

wxRect clControlWithItems::GetItemsRect() const
{
    // Return the rectangle taking header into consideration
    int yOffset = m_viewHeader.GetHeight();
    wxRect clientRect = GetClientArea();
    clientRect.SetY(yOffset);
    clientRect.SetHeight(clientRect.GetHeight() - yOffset);
    return clientRect;
}

void clControlWithItems::RenderHeader(wxDC& dc)
{
    if(IsHeaderVisible()) {
        wxRect headerRect = GetClientArea();
        headerRect.SetHeight(m_viewHeader.GetHeight());
        m_viewHeader.Render(dc, headerRect, m_colours);
    }
}

void clControlWithItems::RenderItems(wxDC& dc, const clRowEntry::Vec_t& items)
{
    AssignRects(items);
    for(size_t i = 0; i < items.size(); ++i) {
        clRowEntry* curitem = items[i];
        if(curitem->IsHidden()) { continue; }
        curitem->Render(this, dc, m_colours, i, &GetSearch());
    }
}

int clControlWithItems::GetNumLineCanFitOnScreen() const
{
    wxRect clientRect = GetItemsRect();
    int max_lines_on_screen = std::ceil((double)((double)clientRect.GetHeight() / (double)m_lineHeight));
    return max_lines_on_screen;
}

clRowEntry* clControlWithItems::GetFirstItemOnScreen() { return m_firstItemOnScreen; }

void clControlWithItems::SetFirstItemOnScreen(clRowEntry* item) { m_firstItemOnScreen = item; }

void clControlWithItems::UpdateScrollBar()
{
    {
        // V-scrollbar
        // wxRect rect = GetItemsRect();
        int thumbSize = GetNumLineCanFitOnScreen(); // Number of lines can be drawn
        int pageSize = (thumbSize);
        int rangeSize = GetRange();
        int position = GetFirstItemPosition();
        UpdateVScrollBar(position, thumbSize, rangeSize, pageSize);
    }
    {
        // H-scrollbar
        int thumbSize = GetClientArea().GetWidth();
        int pageSize = (thumbSize - 1);
        int rangeSize = IsEmpty() ? 0 : m_viewHeader.GetWidth();
        int position = m_firstColumn;
        UpdateHScrollBar(position, thumbSize, rangeSize, pageSize);
    }
}

void clControlWithItems::Render(wxDC& dc)
{
    // draw the background on the entire client area
    dc.SetPen(wxSystemSettings::GetColour(wxSYS_COLOUR_3DFACE));
    dc.SetBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_3DFACE));
    dc.DrawRectangle(GetClientRect());

    // draw the background on the entire client area
    dc.SetPen(GetColours().GetBgColour());
    dc.SetBrush(GetColours().GetBgColour());
    dc.DrawRectangle(GetClientArea());

    // Set the device origin to the X-offset
    dc.SetDeviceOrigin(-m_firstColumn, 0);
    RenderHeader(dc);
}

void clControlWithItems::OnSize(wxSizeEvent& event)
{
    event.Skip();
    m_firstColumn = 0;
    UpdateScrollBar();
    Refresh();
}

void clControlWithItems::ScollToColumn(int firstColumn)
{
    m_firstColumn = firstColumn;
    Refresh();
}

void clControlWithItems::ScrollColumns(int steps, wxDirection direction)
{
    if((steps == 0) && (direction == wxLEFT)) {
        m_firstColumn = 0;
    } else if((steps == 0) && (direction == wxRIGHT)) {
        m_firstColumn = GetHeader().GetWidth();
    } else {
        int max_width = GetHeader().GetWidth();
        int firstColumn = m_firstColumn + ((direction == wxRIGHT) ? steps : -steps);
        if(firstColumn < 0) { firstColumn = 0; }
        int pageSize = GetClientArea().GetWidth();
        if((firstColumn + pageSize) > max_width) { firstColumn = max_width - pageSize; }
        m_firstColumn = firstColumn;
    }
    Refresh();
}

void clControlWithItems::DoUpdateHeader(clRowEntry* row)
{
    // do we have header?
    if(GetHeader().empty()) { return; }
    if(row && row->IsHidden()) { return; }
    wxDC& dc = GetTempDC();

    // Null row means: set the header bar to fit the column's label
    bool forceUpdate = (row == nullptr);

    // Use bold font, to get the maximum width needed
    for(size_t i = 0; i < GetHeader().size(); ++i) {

        int row_width = 0;
        if(row) {
            row_width = row->CalcItemWidth(dc, m_lineHeight, i);
        } else {
            int colWidth = dc.GetTextExtent(GetHeader().Item(i).GetLabel()).GetWidth();
            colWidth += 3 * clRowEntry::X_SPACER;
            row_width = colWidth;
        }
        GetHeader().UpdateColWidthIfNeeded(i, row_width, forceUpdate);
    }
}

wxSize clControlWithItems::GetTextSize(const wxString& label) const
{
    wxDC& dc = GetTempDC();
    wxFont font = GetDefaultFont();
    dc.SetFont(font);
    wxSize textSize = dc.GetTextExtent(label);
    return textSize;
}

const wxBitmap& clControlWithItems::GetBitmap(size_t index) const
{
    if(!GetBitmaps() || (index >= GetBitmaps()->size())) {
        static wxBitmap emptyBitmap;
        return emptyBitmap;
    }
    return GetBitmaps()->at(index);
}

void clControlWithItems::OnMouseScroll(wxMouseEvent& event)
{
    event.Skip();
    DoMouseScroll(event);
}

void clControlWithItems::SetNativeHeader(bool b)
{
    m_viewHeader.SetNative(b);
    Refresh();
}

bool clControlWithItems::DoKeyDown(const wxKeyEvent& event)
{
    if(m_searchControl && m_searchControl->IsShown()) { return true; }
    if(m_search.IsEnabled() && wxIsprint(event.GetUnicodeKey()) &&
       (event.GetModifiers() == wxMOD_NONE || event.GetModifiers() == wxMOD_SHIFT)) {
        if(!m_searchControl) { m_searchControl = new clSearchControl(this); }
        m_searchControl->ShowControl(event.GetUnicodeKey());
        return true;
    }
    return false;
}

void clControlWithItems::SearchControlDismissed() {}

void clControlWithItems::AssignRects(const clRowEntry::Vec_t& items)
{
    wxRect clientRect = GetItemsRect();
    int y = clientRect.GetY();
    for(size_t i = 0; i < items.size(); ++i) {
        clRowEntry* curitem = items[i];
        if(curitem->IsHidden()) {
            // Set the item's rects into something non visible
            curitem->SetRects(wxRect(-100, -100, 0, 0), wxRect(-100, -100, 0, 0));
            continue;
        }
        wxRect itemRect = wxRect(0, y, clientRect.GetWidth(), m_lineHeight);
        wxRect buttonRect;
        if(curitem->HasChildren()) {
            buttonRect = wxRect((curitem->GetIndentsCount() * GetIndent()), y, m_lineHeight, m_lineHeight);
        }
        curitem->SetRects(itemRect, buttonRect);
        y += m_lineHeight;
    }
}

void clControlWithItems::DoMouseScroll(const wxMouseEvent& event)
{
    int range = GetRange();
    bool going_up = (event.GetWheelRotation() > 0);
    int new_row = GetFirstItemPosition() + (going_up ? -GetScrollTick() : GetScrollTick());
    if(new_row < 0) { new_row = 0; }
    if(new_row >= range) { new_row = range - 1; }
    ScrollToRow(new_row);
}

//===---------------------------------------------------
// clSearchText
//===---------------------------------------------------
clSearchText::clSearchText() {}

clSearchText::~clSearchText() {}

bool clSearchText::Matches(const wxString& findWhat, size_t col, const wxString& text, size_t searchFlags,
                           clMatchResult* matches)
{
    wxString haystack = searchFlags & wxTR_SEARCH_ICASE ? text.Lower() : text;
    wxString needle = searchFlags & wxTR_SEARCH_ICASE ? findWhat.Lower() : findWhat;
    if(!matches) {
        if(searchFlags & wxTR_SEARCH_METHOD_CONTAINS) {
            return haystack.Contains(needle);
        } else {
            return (haystack == needle);
        }
    } else {
        if(searchFlags & wxTR_SEARCH_METHOD_CONTAINS) {
            int where = haystack.Find(needle);
            if(where == wxNOT_FOUND) { return false; }
            Str3Arr_t arr;
            arr[0] = text.Mid(0, where);
            arr[1] = text.Mid(where, needle.length());
            arr[2] = text.Mid(where + needle.length());
            matches->Add(col, arr);
            return true;
        } else {
            if(haystack == needle) {
                Str3Arr_t arr;
                arr[0] = "";
                arr[1] = text;
                arr[2] = "";
                matches->Add(col, arr);
                return true;
            } else {
                return false;
            }
        }
    }
    return false;
}