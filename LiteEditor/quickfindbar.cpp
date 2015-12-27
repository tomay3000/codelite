//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//
// copyright            : (C) 2008 by Eran Ifrah
// file name            : quickfindbar.cpp
//
// -------------------------------------------------------------------------
// A
//              _____           _      _     _ _
//             /  __ \         | |    | |   (_) |
//             | /  \/ ___   __| | ___| |    _| |_ ___
//             | |    / _ \ / _  |/ _ \ |   | | __/ _ )
//             | \__/\ (_) | (_| |  __/ |___| | ||  __/
//              \____/\___/ \__,_|\___\_____/_|\__\___|
//
//                                                  F i l e
//
//    This program is free software; you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation; either version 2 of the License, or
//    (at your option) any later version.
//
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
#include <wx/xrc/xmlres.h>
#include <wx/regex.h>
#include "editor_config.h"
#include <wx/statline.h>
#include "manager.h"
#include "frame.h"
#include <wx/textctrl.h>
#include <wx/stc/stc.h>
#include "stringsearcher.h"
#include "quickfindbar.h"
#include "event_notifier.h"
#include "plugin.h"
#include "cl_config.h"
#include <wx/gdicmn.h>
#include "wxFlatButtonBar.h"
#include "globals.h"

DEFINE_EVENT_TYPE(QUICKFIND_COMMAND_EVENT)

#define CHECK_FOCUS_WIN()                           \
    {                                               \
        wxWindow* focus = wxWindow::FindFocus();    \
        if(focus != m_sci && focus != m_findWhat) { \
            e.Skip();                               \
            return;                                 \
        }                                           \
                                                    \
        if(!m_sci || m_sci->GetLength() == 0) {     \
            e.Skip();                               \
            return;                                 \
        }                                           \
    }

void PostCommandEvent(wxWindow* destination, wxWindow* FocusedControl)
{
    // Posts an event that signals for SelectAll() to be done after a delay
    // This is often needed in >2.9, as scintilla seems to steal the selection
    const static int DELAY_COUNT = 10;

    wxCommandEvent event(QUICKFIND_COMMAND_EVENT);
    event.SetInt(DELAY_COUNT);
    event.SetEventObject(FocusedControl);
    wxPostEvent(destination, event);
}

QuickFindBar::QuickFindBar(wxWindow* parent, wxWindowID id)
    : QuickFindBarBase(parent, id)
    , m_sci(NULL)
    , m_flags(0)
    , m_lastTextPtr(NULL)
    , m_eventsConnected(false)
    , m_optionsWindow(NULL)
    , m_regexType(kRegexNone)
    , m_disableTextUpdateEvent(false)
{
    m_bar = new wxFlatButtonBar(this, wxFlatButton::kThemeNormal, 0, 9);

    //-------------------------------------------------------------
    // Find / Replace bar
    //-------------------------------------------------------------
    // [x][A]["][*][/][..............][find][find prev][find all]
    // [-][-][-][-][-][..............][replace]
    //-------------------------------------------------------------
    m_bar->SetExpandableColumn(5);
    GetSizer()->Add(m_bar, 1, wxEXPAND | wxALL, 2);
    QuickFindBarImages images;

    // Add the 'close' button
    m_closeButton = m_bar->AddButton("", images.Bitmap("find-bar-close-16"), wxSize(24, -1));
    m_closeButton->SetToolTip(_("Close"));
    m_closeButton->Bind(wxEVT_KEY_DOWN, &QuickFindBar::OnKeyDown, this);
    m_closeButton->Bind(wxEVT_CMD_FLATBUTTON_CLICK, &QuickFindBar::OnHideBar, this);

    // Add the 'case sensitive' button
    m_caseSensitive = m_bar->AddButton("", images.Bitmap("case-sensitive"), wxSize(24, -1));
    m_caseSensitive->SetTogglable(true);
    m_caseSensitive->SetToolTip(_("Case sensitive match"));
    m_caseSensitive->Bind(wxEVT_KEY_DOWN, &QuickFindBar::OnKeyDown, this);

    // Add the 'whole word' button
    m_wholeWord = m_bar->AddButton("", images.Bitmap("word"), wxSize(24, -1));
    m_wholeWord->SetTogglable(true);
    m_wholeWord->SetToolTip(_("Match a whole word"));
    m_wholeWord->Bind(wxEVT_KEY_DOWN, &QuickFindBar::OnKeyDown, this);

    // Regex or Wild card syntax?
    m_regexOrWildMenu = new wxMenu;
    m_regexOrWildMenu->Append(ID_MENU_NO_REGEX, _("None"), _("None"), wxITEM_CHECK);
    m_regexOrWildMenu->Append(ID_MENU_REGEX, _("Regular expression"), _("Regular expression"), wxITEM_CHECK);
    m_regexOrWildMenu->Append(ID_MENU_WILDCARD, _("Wildcard syntax"), _("Wildcard syntax"), wxITEM_CHECK);

    m_regexOrWildButton = m_bar->AddButton("", images.Bitmap("regex"), wxSize(24, -1));
    m_regexOrWildButton->SetTogglable(true);
    m_regexOrWildButton->Bind(wxEVT_CMD_FLATBUTTON_CLICK, &QuickFindBar::OnRegex, this);
    m_regexOrWildButton->Bind(wxEVT_UPDATE_UI, &QuickFindBar::OnRegexUI, this);
    m_regexOrWildButton->Bind(wxEVT_KEY_DOWN, &QuickFindBar::OnKeyDown, this);
    m_regexOrWildButton->SetToolTip(_("Use regular expression"));

    // Marker button
    wxFlatButton* btnMarker = m_bar->AddButton("", images.Bitmap("marker-16"), wxSize(24, -1));
    btnMarker->SetTogglable(true);
    btnMarker->Bind(wxEVT_CMD_FLATBUTTON_CLICK, &QuickFindBar::OnHighlightMatches, this);
    btnMarker->Bind(wxEVT_UPDATE_UI, &QuickFindBar::OnHighlightMatchesUI, this);
    btnMarker->Bind(wxEVT_KEY_DOWN, &QuickFindBar::OnKeyDown, this);
    btnMarker->SetToolTip(_("Highlight Occurences"));

    //=======----------------------
    // Find what:
    //=======----------------------

    wxArrayString m_findWhatArr;
    m_findWhat =
        new wxComboBox(m_bar, wxID_ANY, wxT(""), wxDefaultPosition, wxSize(-1, -1), m_findWhatArr, wxTE_PROCESS_ENTER);
    m_findWhat->SetToolTip(_("Hit ENTER to search, or Shift + ENTER to search backward"));
    m_findWhat->SetFocus();
    m_findWhat->SetHint(_("Type to start a search..."));
    m_bar->AddControl(m_findWhat, 1, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL);

    // Find
    wxFlatButton* btnNext = m_bar->AddButton(_("Find"), wxNullBitmap, wxSize(100, -1), wxBORDER_SIMPLE);
    btnNext->Bind(wxEVT_CMD_FLATBUTTON_CLICK, &QuickFindBar::OnButtonNext, this);
    btnNext->Bind(wxEVT_KEY_DOWN, &QuickFindBar::OnKeyDown, this);
    btnNext->Bind(wxEVT_UPDATE_UI, &QuickFindBar::OnButtonNextUI, this);
    btnNext->SetToolTip(_("Find Next"));

    // Find Prev
    wxFlatButton* btnPrev = m_bar->AddButton(_("Find Prev"), wxNullBitmap, wxSize(100, -1), wxBORDER_SIMPLE);
    btnPrev->Bind(wxEVT_CMD_FLATBUTTON_CLICK, &QuickFindBar::OnButtonPrev, this);
    btnPrev->Bind(wxEVT_KEY_DOWN, &QuickFindBar::OnKeyDown, this);
    btnPrev->Bind(wxEVT_UPDATE_UI, &QuickFindBar::OnButtonPrevUI, this);
    btnPrev->SetToolTip(_("Find Previous"));

    // Find All
    wxFlatButton* btnAll = m_bar->AddButton(_("Find All"), wxNullBitmap, wxSize(100, -1), wxBORDER_SIMPLE);
    btnAll->Bind(wxEVT_CMD_FLATBUTTON_CLICK, &QuickFindBar::OnFindAll, this);
    btnAll->Bind(wxEVT_UPDATE_UI, &QuickFindBar::OnButtonPrevUI, this);
    btnAll->SetToolTip(_("Find and select all occurrences"));

    //=======----------------------
    // Replace with (new row)
    //=======----------------------
    // We first need to add 5 spacers
    m_bar->AddSpacer(0);
    m_bar->AddSpacer(0);
    m_bar->AddSpacer(0);
    m_bar->AddSpacer(0);
    m_bar->AddSpacer(0);

    wxArrayString m_replaceWithArr;
    m_replaceWith = new wxComboBox(
        m_bar, wxID_ANY, wxT(""), wxDefaultPosition, wxSize(-1, -1), m_replaceWithArr, wxTE_PROCESS_ENTER);
    m_replaceWith->SetToolTip(_("Type the replacement string and hit ENTER to perform the replacement"));
    m_replaceWith->SetHint(_("Type any replacement string..."));
    m_bar->AddControl(m_replaceWith, 1, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL);

    m_buttonReplace = m_bar->AddButton(_("Replace"), wxNullBitmap, wxSize(100, -1), wxBORDER_SIMPLE);
    m_buttonReplace->SetToolTip(_("Replace the current selection"));
    m_buttonReplace->Bind(wxEVT_CMD_FLATBUTTON_CLICK, &QuickFindBar::OnButtonReplace, this);
    m_buttonReplace->Bind(wxEVT_UPDATE_UI, &QuickFindBar::OnButtonReplaceUI, this);
    m_buttonReplace->Bind(wxEVT_KEY_DOWN, &QuickFindBar::OnKeyDown, this);

    bool showreplace = EditorConfigST::Get()->GetOptions()->GetShowReplaceBar();
    m_replaceWith->Show(showreplace); // Hide the replace-bar if desired
    m_buttonReplace->Show(showreplace);

    // Connect the events
    m_findWhat->Bind(wxEVT_COMMAND_TEXT_ENTER, &QuickFindBar::OnEnter, this);
    m_findWhat->Bind(wxEVT_COMMAND_TEXT_UPDATED, &QuickFindBar::OnText, this);
    m_findWhat->Bind(wxEVT_KEY_DOWN, &QuickFindBar::OnKeyDown, this);
    m_findWhat->Bind(wxEVT_MOUSEWHEEL, &QuickFindBar::OnFindMouseWheel, this);
    m_replaceWith->Bind(wxEVT_KEY_DOWN, &QuickFindBar::OnReplaceKeyDown, this);
    m_replaceWith->Bind(wxEVT_COMMAND_TEXT_ENTER, &QuickFindBar::OnReplace, this);
    btnNext->Bind(wxEVT_KEY_DOWN, &QuickFindBar::OnKeyDown, this);

    Hide();
    GetSizer()->Fit(this);
    wxTheApp->Connect(
        XRCID("find_next"), wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(QuickFindBar::OnFindNext), NULL, this);
    wxTheApp->Connect(XRCID("find_previous"), wxEVT_COMMAND_MENU_SELECTED,
        wxCommandEventHandler(QuickFindBar::OnFindPrevious), NULL, this);
    wxTheApp->Connect(XRCID("find_next_at_caret"), wxEVT_COMMAND_MENU_SELECTED,
        wxCommandEventHandler(QuickFindBar::OnFindNextCaret), NULL, this);
    wxTheApp->Connect(XRCID("find_previous_at_caret"), wxEVT_COMMAND_MENU_SELECTED,
        wxCommandEventHandler(QuickFindBar::OnFindPreviousCaret), NULL, this);

    EventNotifier::Get()->Connect(
        wxEVT_FINDBAR_RELEASE_EDITOR, wxCommandEventHandler(QuickFindBar::OnReleaseEditor), NULL, this);
    Connect(QUICKFIND_COMMAND_EVENT, wxCommandEventHandler(QuickFindBar::OnQuickFindCommandEvent), NULL, this);

    // Initialize the list with the history
    m_findWhat->Append(clConfig::Get().GetQuickFindSearchItems());
    m_replaceWith->Append(clConfig::Get().GetQuickFindReplaceItems());
}

bool QuickFindBar::Show(bool show)
{
    if(!m_sci && show) {
        return false;
    }
    return DoShow(show, wxEmptyString);
}

void QuickFindBar::ToggleReplacebar()
{
    if(!m_sci || !IsShown()) {
        return;
    }
    DoToggleReplacebar();
}

void QuickFindBar::DoSearch(size_t searchFlags, int posToSearchFrom)
{
    if(!m_sci || m_sci->GetLength() == 0 || m_findWhat->GetValue().IsEmpty()) return;
    
    clGetManager()->SetStatusMessage(wxEmptyString);
    
    // Clear all search markers if desired
    if(EditorConfigST::Get()->GetOptions()->GetClearHighlitWordsOnFind()) {
        m_sci->SetIndicatorCurrent(MARKER_WORD_HIGHLIGHT);
        m_sci->IndicatorClearRange(0, m_sci->GetLength());
    }

    wxString find = m_findWhat->GetValue();
    bool fwd = searchFlags & kSearchForward;
    int flags = DoGetSearchFlags();
    int curpos = m_sci->GetCurrentPos();
    int start = wxNOT_FOUND;
    int end = wxNOT_FOUND;
    m_sci->GetSelection(&start, &end);
    bool restoreSelection = false;
    if((end != wxNOT_FOUND) && fwd) {
        if(m_sci->FindText(start, end, find, flags) != wxNOT_FOUND) {
            // Incase we searching forward and the current selection matches the search string
            // Clear the selection and set the caret position to the end of the selection
            m_sci->SetCurrentPos(end);
            m_sci->SetSelectionEnd(end);
            m_sci->SetSelectionStart(end);
            restoreSelection = true;
        }
    }

    int pos = wxNOT_FOUND;
    if(fwd) {
        m_sci->SearchAnchor();
        pos = m_sci->SearchNext(flags, find);
        if(pos == wxNOT_FOUND) {
            clGetManager()->SetStatusMessage(_("Wrapped past end of file"), 1);
            m_sci->SetCurrentPos(0);
            m_sci->SetSelectionEnd(0);
            m_sci->SetSelectionStart(0);
            m_sci->SearchAnchor();
            pos = m_sci->SearchNext(flags, find);
        }
    } else {
        m_sci->SearchAnchor();
        pos = m_sci->SearchPrev(flags, find);
        if(pos == wxNOT_FOUND) {
            clGetManager()->SetStatusMessage(_("Wrapped past end of file"), 1);
            int lastPos = m_sci->GetLastPosition();
            m_sci->SetCurrentPos(lastPos);
            m_sci->SetSelectionEnd(lastPos);
            m_sci->SetSelectionStart(lastPos);
            m_sci->SearchAnchor();
            pos = m_sci->SearchPrev(flags, find);
        }
    }

    if(pos == wxNOT_FOUND) {
        // Restore the caret position
        m_sci->SetCurrentPos(curpos);
        m_sci->ClearSelections();
        return;
    }

    int line = m_sci->LineFromPosition(m_sci->GetSelectionStart());
    int linesOnScreen = m_sci->LinesOnScreen();
    // To place our line in the middle, the first visible line should be
    // the: line - (linesOnScreen / 2)
    int firstVisibleLine = line - (linesOnScreen / 2);
    if(firstVisibleLine < 0) {
        firstVisibleLine = 0;
    }
    m_sci->EnsureVisible(firstVisibleLine);
    m_sci->SetFirstVisibleLine(firstVisibleLine);
}

void QuickFindBar::OnHide(wxCommandEvent& e)
{
    // Kill any "...continued from start" statusbar message
    clMainFrame::Get()->GetStatusBar()->SetMessage(wxEmptyString);

    Show(false);
    e.Skip();
}

void QuickFindBar::OnNext(wxCommandEvent& e)
{
    wxUnusedVar(e);
    if(!m_findWhat->GetValue().IsEmpty()) {
        clConfig::Get().AddQuickFindSearchItem(m_findWhat->GetValue());
        // Update the search history
        DoUpdateSearchHistory();
    }
    size_t flags = kSearchForward;
    DoSearch(flags);
}

void QuickFindBar::OnPrev(wxCommandEvent& e)
{
    wxUnusedVar(e);
    if(!m_findWhat->GetValue().IsEmpty()) {
        clConfig::Get().AddQuickFindSearchItem(m_findWhat->GetValue());
        // Update the search history
        DoUpdateSearchHistory();
    }
    size_t flags = 0;
    DoSearch(flags);
}

void QuickFindBar::OnText(wxCommandEvent& e)
{
    e.Skip();
    if(!m_disableTextUpdateEvent) {
        CallAfter(&QuickFindBar::DoSearch, kSearchForward | kSearchIncremental, -1);
    }
}

void QuickFindBar::OnKeyDown(wxKeyEvent& e)
{
    switch(e.GetKeyCode()) {
    case WXK_ESCAPE: {
        wxCommandEvent dummy;
        OnHide(dummy);
        break;
    }
    default: {
        e.Skip();
        break;
    }
    }
}

void QuickFindBar::OnUpdateUI(wxUpdateUIEvent& e)
{
    e.Enable(ManagerST::Get()->IsShutdownInProgress() == false && m_sci && m_sci->GetLength() > 0 &&
        !m_findWhat->GetValue().IsEmpty());
}

void QuickFindBar::OnEnter(wxCommandEvent& e)
{
    wxUnusedVar(e);

    if(!m_findWhat->GetValue().IsEmpty()) {
        clConfig::Get().AddQuickFindSearchItem(m_findWhat->GetValue());
        // Update the search history
        DoUpdateSearchHistory();
    }

    bool shift = wxGetKeyState(WXK_SHIFT);
    if(shift) {
        OnPrev(e);
    } else {
        OnNext(e);
    }
}

void QuickFindBar::OnCopy(wxCommandEvent& e)
{
    e.Skip(false);
    if(m_findWhat->HasFocus() && m_findWhat->CanCopy()) {
        m_findWhat->Copy();

    } else if(m_replaceWith->HasFocus() && m_replaceWith->CanCopy()) {
        m_replaceWith->Copy();

    } else {
        e.Skip();
    }
}

void QuickFindBar::OnPaste(wxCommandEvent& e)
{
    e.Skip(false);
    if(m_findWhat->HasFocus() && m_findWhat->CanPaste()) {
        m_findWhat->Paste();

    } else if(m_replaceWith->HasFocus() && m_replaceWith->CanPaste()) {
        m_replaceWith->Paste();

    } else {
        e.Skip();
    }
}

void QuickFindBar::OnSelectAll(wxCommandEvent& e)
{
    e.Skip(false);
    if(m_findWhat->HasFocus()) {
        m_findWhat->SelectAll();

    } else if(m_replaceWith->HasFocus()) {
        m_replaceWith->SelectAll();

    } else {
        e.Skip();
    }
}

void QuickFindBar::OnEditUI(wxUpdateUIEvent& e) {}

void QuickFindBar::OnReplace(wxCommandEvent& event)
{
    wxUnusedVar(event);
    if(!m_sci) return;

    // if there is no selection, invoke search
    int nNumSelections = m_sci->GetSelections();
#ifndef __WXMAC__
    int re_flags = wxRE_ADVANCED;
#else
    int re_flags = wxRE_DEFAULT;
#endif

    bool caseSearch = m_flags & wxSD_MATCHCASE;
    wxString selectionText;
    if(nNumSelections == 1) {
        selectionText = m_sci->GetSelectedText();

    } else if(nNumSelections > 1) {
        selectionText = DoGetSelectedText();
    }

    if(selectionText.IsEmpty()) return;

    wxString find = m_findWhat->GetValue();
    wxString replaceWith = m_replaceWith->GetValue();

    if(!caseSearch) {
        selectionText.MakeLower();
        find.MakeLower();
    }

    if(find.IsEmpty()) return;

    if(!replaceWith.IsEmpty()) {
        clConfig::Get().AddQuickFindReplaceItem(replaceWith);
        DoUpdateReplaceHistory();
    }

    int nextSearchOffset = m_sci->GetSelectionStart() + replaceWith.Length();

    // do we got a match?
    if((selectionText != find) && !(m_flags & wxSD_REGULAREXPRESSION)) {
        size_t flags = kSearchForward | kSearchIncremental;
        DoSearch(flags);

    } else if(m_flags & wxSD_REGULAREXPRESSION) {
        // regular expression search
        wxString selectedText = selectionText;

        // handle back references (\1 \2 etc)
        if(m_sci && selectedText.IsEmpty() == false) {

            // search was regular expression search
            // handle any back references
            caseSearch == false ? re_flags |= wxRE_ICASE : re_flags;
            wxRegEx re(find, re_flags);
            if(re.IsValid() && re.Matches(selectedText)) {
                re.Replace(&selectedText, replaceWith);
            }

            m_sci->BeginUndoAction();
            for(int i = 0; i < nNumSelections; ++i) {
                int nStart = m_sci->GetSelectionNStart(i);
                int nEnd = m_sci->GetSelectionNEnd(i);
                if(nEnd > nStart) {
                    m_sci->Replace(nStart, nEnd, selectedText);
                }
            }
            m_sci->EndUndoAction();
            m_sci->ClearSelections();
        }

        // and search again
        if(nNumSelections == 1) {
            size_t flags = kSearchForward | kSearchIncremental;
            DoSearch(flags, nextSearchOffset);
        }

    } else {

        m_sci->BeginUndoAction();
        for(int i = 0; i < nNumSelections; ++i) {
            int nStart = m_sci->GetSelectionNStart(i);
            int nEnd = m_sci->GetSelectionNEnd(i);
            if(nEnd > nStart) {
                m_sci->Replace(nStart, nEnd, replaceWith);
            }
        }
        m_sci->EndUndoAction();
        m_sci->ClearSelections();

        // and search again
        if(nNumSelections == 1) {
            size_t flags = kSearchForward | kSearchIncremental;
            DoSearch(flags, nextSearchOffset);
        }
    }
}

void QuickFindBar::OnReplaceUI(wxUpdateUIEvent& e)
{
    e.Enable(ManagerST::Get()->IsShutdownInProgress() == false && m_sci && !m_sci->GetReadOnly() &&
        m_sci->GetLength() > 0 && !m_findWhat->GetValue().IsEmpty());
}

void QuickFindBar::OnReplaceEnter(wxCommandEvent& e)
{
    wxUnusedVar(e);
    wxCommandEvent evt(wxEVT_COMMAND_TOOL_CLICKED, ID_TOOL_REPLACE);
    GetEventHandler()->AddPendingEvent(evt);
}

void QuickFindBar::SetEditor(wxStyledTextCtrl* sci)
{
    m_sci = sci;
    if(!m_sci) {
        DoShow(false, "");
        return;
    }
}

int QuickFindBar::GetCloseButtonId() { return ID_TOOL_CLOSE; }

bool QuickFindBar::Show(const wxString& findWhat)
{
    // Same as Show() but set the 'findWhat' field with findWhat
    if(!m_sci) return false;

    return DoShow(true, findWhat);
}

bool QuickFindBar::DoShow(bool s, const wxString& findWhat)
{
    bool res = wxPanel::Show(s);

    if(s && !m_eventsConnected) {
        BindEditEvents(true);

    } else if(m_eventsConnected) {
        BindEditEvents(false);
    }

    if(s && m_sci) {
        // Delete the indicators
        m_sci->SetIndicatorCurrent(1);
        m_sci->IndicatorClearRange(0, m_sci->GetLength());

        if(EditorConfigST::Get()->GetOptions()->GetClearHighlitWordsOnFind()) {
            m_sci->SetIndicatorCurrent(MARKER_WORD_HIGHLIGHT);
            m_sci->IndicatorClearRange(0, m_sci->GetLength());
        }
    }

    if(res) {
        GetParent()->GetSizer()->Layout();
    }

    if(!m_sci) {
        // nothing to do

    } else if(!s) {
        // hiding
        m_sci->SetFocus();

    } else if(!findWhat.IsEmpty()) {

        m_findWhat->ChangeValue(findWhat);
        m_findWhat->SelectAll();
        m_findWhat->SetFocus();
        PostCommandEvent(this, m_findWhat);

    } else {
        if(m_sci->GetSelections() > 1) {
        }
        wxString findWhat = DoGetSelectedText().BeforeFirst(wxT('\n'));
        if(!findWhat.IsEmpty()) {
            m_findWhat->ChangeValue(findWhat);
        }

        m_findWhat->SelectAll();
        m_findWhat->SetFocus();
        PostCommandEvent(this, m_findWhat);
    }
    return res;
}

void QuickFindBar::DoToggleReplacebar()
{
    OptionsConfigPtr options = EditorConfigST::Get()->GetOptions();
    bool show = !options->GetShowReplaceBar();

    options->SetShowReplaceBar(show);
    EditorConfigST::Get()->SetOptions(options);

    ShowReplacebar(show);
}

void QuickFindBar::ShowReplacebar(bool show)
{
    m_replaceWith->Show(show);
    m_buttonReplace->Show(show);
    m_bar->GetSizer()->Layout();
    if(IsShown()) {
        clMainFrame::Get()->SendSizeEvent(); // Needed to show/hide the 'replace' bar itself
    }
}

void QuickFindBar::OnFindNext(wxCommandEvent& e)
{
    CHECK_FOCUS_WIN();

    // Highlighted text takes precedence over the current search string
    wxString selectedText = DoGetSelectedText();
    if(selectedText.IsEmpty() == false) {
        m_findWhat->ChangeValue(selectedText);
        m_findWhat->SelectAll();
    }

    DoSearch(kSearchForward);
}

void QuickFindBar::OnFindPrevious(wxCommandEvent& e)
{
    CHECK_FOCUS_WIN();

    // Highlighted text takes precedence over the current search string
    wxString selectedText = DoGetSelectedText();
    if(selectedText.IsEmpty() == false) {
        m_findWhat->ChangeValue(selectedText);
        m_findWhat->SelectAll();
    }

    DoSearch(0);
}

void QuickFindBar::OnFindNextCaret(wxCommandEvent& e)
{
    CHECK_FOCUS_WIN();

    wxString selection(DoGetSelectedText());
    if(selection.IsEmpty()) {
        // select the word
        long pos = m_sci->GetCurrentPos();
        long start = m_sci->WordStartPosition(pos, true);
        long end = m_sci->WordEndPosition(pos, true);

        selection = m_sci->GetTextRange(start, end);
        if(selection.IsEmpty() == false) m_sci->SetCurrentPos(start);
    }

    if(selection.IsEmpty()) return;

    m_findWhat->ChangeValue(selection);
    DoSearch(kSearchForward);
}

void QuickFindBar::OnFindPreviousCaret(wxCommandEvent& e)
{
    CHECK_FOCUS_WIN();

    wxString selection(DoGetSelectedText());
    if(selection.IsEmpty()) {
        // select the word
        long pos = m_sci->GetCurrentPos();
        long start = m_sci->WordStartPosition(pos, true);
        long end = m_sci->WordEndPosition(pos, true);

        selection = m_sci->GetTextRange(start, end);
        if(selection.IsEmpty() == false) m_sci->SetCurrentPos(start);
    }

    if(selection.IsEmpty()) return;

    m_findWhat->ChangeValue(selection);
    DoSearch(0);
}

void QuickFindBar::DoMarkAll()
{
    wxCommandEvent evt(wxEVT_MENU, XRCID("ID_QUICK_FIND_ALL"));
    clMainFrame::Get()->GetEventHandler()->AddPendingEvent(evt);
    Show(false);
}

void QuickFindBar::OnHighlightMatches(wxFlatButtonEvent& e)
{
    bool checked = e.IsChecked();
    LEditor* editor = dynamic_cast<LEditor*>(m_sci);
    if(checked && editor) {
        editor->SetFindBookmarksActive(true);
        DoMarkAll();

    } else {
        if(editor) {
            editor->DelAllMarkers(smt_find_bookmark);
            editor->SetFindBookmarksActive(false);
        }
    }

    clMainFrame::Get()->SelectBestEnvSet(); // Updates the statusbar display
}

void QuickFindBar::OnHighlightMatchesUI(wxUpdateUIEvent& event)
{
    if(ManagerST::Get()->IsShutdownInProgress()) {
        event.Enable(false);

    } else if(!m_sci) {
        event.Enable(false);

    } else if(m_findWhat->GetValue().IsEmpty()) {
        event.Enable(false);

    } else {
        LEditor* editor = dynamic_cast<LEditor*>(m_sci);
        if(!editor) {
            event.Enable(false);

        } else {
            // Check to see if there are any markers
            int nLine = editor->LineFromPosition(0);
            int nFoundLine = editor->MarkerNext(nLine, mmt_find_bookmark);

            event.Enable(true);
            event.Check(nFoundLine != wxNOT_FOUND);
        }
    }
}

void QuickFindBar::OnReceivingFocus(wxFocusEvent& event)
{
    event.Skip();
    if((event.GetEventObject() == m_findWhat) || (event.GetEventObject() == m_replaceWith)) {
        PostCommandEvent(this, wxStaticCast(event.GetEventObject(), wxWindow));
    }
}

void QuickFindBar::OnQuickFindCommandEvent(wxCommandEvent& event)
{
    if(event.GetInt() > 0) {
        // We need to delay further, or focus might be set too soon
        event.SetInt(event.GetInt() - 1);
        wxPostEvent(this, event);
    }

    if(event.GetEventObject() == m_findWhat) {
        m_findWhat->SetFocus();
        m_findWhat->SelectAll();

    } else if(event.GetEventObject() == m_replaceWith) {
        m_replaceWith->SetFocus();
        m_replaceWith->SelectAll();
    }
}

QuickFindBar::~QuickFindBar()
{
    wxTheApp->Disconnect(
        XRCID("find_next"), wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(QuickFindBar::OnFindNext), NULL, this);
    wxTheApp->Disconnect(XRCID("find_previous"), wxEVT_COMMAND_MENU_SELECTED,
        wxCommandEventHandler(QuickFindBar::OnFindPrevious), NULL, this);
    wxTheApp->Disconnect(XRCID("find_next_at_caret"), wxEVT_COMMAND_MENU_SELECTED,
        wxCommandEventHandler(QuickFindBar::OnFindNextCaret), NULL, this);
    wxTheApp->Disconnect(XRCID("find_previous_at_caret"), wxEVT_COMMAND_MENU_SELECTED,
        wxCommandEventHandler(QuickFindBar::OnFindPreviousCaret), NULL, this);
    EventNotifier::Get()->Disconnect(
        wxEVT_FINDBAR_RELEASE_EDITOR, wxCommandEventHandler(QuickFindBar::OnReleaseEditor), NULL, this);
}

void QuickFindBar::OnReleaseEditor(wxCommandEvent& e)
{
    wxStyledTextCtrl* win = reinterpret_cast<wxStyledTextCtrl*>(e.GetClientData());
    if(win && win == m_sci) {
        m_sci = NULL;
        Show(false);
    }
}

wxStyledTextCtrl* QuickFindBar::DoCheckPlugins()
{
    // Let the plugins a chance to provide their own window
    wxCommandEvent evt(wxEVT_FINDBAR_ABOUT_TO_SHOW);
    evt.SetClientData(NULL);
    EventNotifier::Get()->ProcessEvent(evt);

    wxStyledTextCtrl* win = reinterpret_cast<wxStyledTextCtrl*>(evt.GetClientData());
    return win;
}

bool QuickFindBar::ShowForPlugins()
{
    m_sci = DoCheckPlugins();
    if(!m_sci) {
        return DoShow(false, "");
    } else {
        return DoShow(true, "");
    }
}

wxString QuickFindBar::DoGetSelectedText()
{
    if(!m_sci) {
        return wxEmptyString;
    }

    if(m_sci->GetSelections() > 1) {
        for(int i = 0; i < m_sci->GetSelections(); ++i) {
            int selStart = m_sci->GetSelectionNStart(i);
            int selEnd = m_sci->GetSelectionNEnd(i);
            if(selEnd > selStart) {
                return m_sci->GetTextRange(selStart, selEnd);
            }
        }
        return wxEmptyString;

    } else {
        return m_sci->GetSelectedText();
    }
}

void QuickFindBar::BindEditEvents(bool bind)
{
    if(bind) {
        clMainFrame::Get()->Bind(wxEVT_COMMAND_MENU_SELECTED, &QuickFindBar::OnCopy, this, wxID_COPY);
        clMainFrame::Get()->Bind(wxEVT_COMMAND_MENU_SELECTED, &QuickFindBar::OnPaste, this, wxID_PASTE);
        clMainFrame::Get()->Bind(wxEVT_COMMAND_MENU_SELECTED, &QuickFindBar::OnSelectAll, this, wxID_SELECTALL);
        clMainFrame::Get()->Bind(wxEVT_COMMAND_MENU_SELECTED, &QuickFindBar::OnUndo, this, wxID_UNDO);
        clMainFrame::Get()->Bind(wxEVT_COMMAND_MENU_SELECTED, &QuickFindBar::OnRedo, this, wxID_REDO);
        m_eventsConnected = true;

    } else {
        clMainFrame::Get()->Unbind(wxEVT_COMMAND_MENU_SELECTED, &QuickFindBar::OnCopy, this, wxID_COPY);
        clMainFrame::Get()->Unbind(wxEVT_COMMAND_MENU_SELECTED, &QuickFindBar::OnPaste, this, wxID_PASTE);
        clMainFrame::Get()->Unbind(wxEVT_COMMAND_MENU_SELECTED, &QuickFindBar::OnSelectAll, this, wxID_SELECTALL);
        clMainFrame::Get()->Unbind(wxEVT_COMMAND_MENU_SELECTED, &QuickFindBar::OnUndo, this, wxID_UNDO);
        clMainFrame::Get()->Unbind(wxEVT_COMMAND_MENU_SELECTED, &QuickFindBar::OnRedo, this, wxID_REDO);
        m_eventsConnected = false;
    }
}

void QuickFindBar::OnRedo(wxCommandEvent& e)
{
    e.Skip(false);
    if(m_findWhat->HasFocus()) {
        if(m_findWhat->CanRedo()) {
            m_findWhat->Redo();
        }
    } else if(m_replaceWith->HasFocus()) {
        if(m_replaceWith->CanRedo()) {
            m_replaceWith->Redo();
        }
    } else {
        e.Skip(true);
    }
}

void QuickFindBar::OnUndo(wxCommandEvent& e)
{
    e.Skip(false);
    if(m_findWhat->HasFocus()) {
        if(m_findWhat->CanUndo()) {
            m_findWhat->Undo();
        }
    } else if(m_replaceWith->HasFocus()) {
        if(m_replaceWith->CanUndo()) {
            m_replaceWith->Undo();
        }
    } else {
        e.Skip(true);
    }
}

void QuickFindBar::OnReplaceKeyDown(wxKeyEvent& event)
{
    switch(event.GetKeyCode()) {
    case WXK_ESCAPE: {
        wxCommandEvent dummy;
        OnHide(dummy);
        break;
    }
    default: {
        event.Skip();
        break;
    }
    }
}

void QuickFindBar::DoUpdateSearchHistory()
{
    wxString findWhat = m_findWhat->GetValue();
    if(findWhat.IsEmpty()) return;
    m_disableTextUpdateEvent = true;
    m_findWhat->Clear();
    m_findWhat->ChangeValue(findWhat);
    m_findWhat->Append(clConfig::Get().GetQuickFindSearchItems());
    m_disableTextUpdateEvent = false;
}

void QuickFindBar::DoUpdateReplaceHistory()
{
    m_disableTextUpdateEvent = true;
    int where = m_replaceWith->FindString(m_replaceWith->GetValue());
    if(where == wxNOT_FOUND) {
        m_replaceWith->Insert(m_replaceWith->GetValue(), 0);
    }
    m_disableTextUpdateEvent = false;
}

void QuickFindBar::OnButtonNext(wxFlatButtonEvent& e) { OnNext(e); }
void QuickFindBar::OnButtonPrev(wxFlatButtonEvent& e) { OnPrev(e); }
void QuickFindBar::OnButtonNextUI(wxUpdateUIEvent& e) { e.Enable(!m_findWhat->GetValue().IsEmpty()); }
void QuickFindBar::OnButtonPrevUI(wxUpdateUIEvent& e) { e.Enable(!m_findWhat->GetValue().IsEmpty()); }
size_t QuickFindBar::DoGetSearchFlags()
{
    m_flags = 0;
    if(m_caseSensitive->IsChecked()) m_flags |= wxSTC_FIND_MATCHCASE;
    if(m_regexType == kRegexPosix) m_flags |= wxSTC_FIND_REGEXP;
    if(m_wholeWord->IsChecked()) m_flags |= wxSTC_FIND_WHOLEWORD;
    return m_flags;
}

void QuickFindBar::OnFindAll(wxFlatButtonEvent& e) { DoMarkAll(); }

void QuickFindBar::OnButtonReplace(wxFlatButtonEvent& e) { OnReplace(e); }

void QuickFindBar::OnButtonReplaceUI(wxUpdateUIEvent& e) { e.Enable(!m_findWhat->GetValue().IsEmpty()); }

void QuickFindBar::OnHideBar(wxFlatButtonEvent& e) { OnHide(e); }

void QuickFindBar::OnFindMouseWheel(wxMouseEvent& e)
{
    // Do nothing and disable the mouse wheel
    // by not calling 'skip'
    wxUnusedVar(e);
}

void QuickFindBar::OnRegex(wxFlatButtonEvent& event) { m_regexType = event.IsChecked() ? kRegexPosix : kRegexNone; }

void QuickFindBar::OnRegexUI(wxUpdateUIEvent& event) { event.Check(m_regexType == kRegexPosix); }
