# Accessibility

English | [简体中文](zh-CN/accessibility.md)

Pt exposes a platform-neutral accessibility model and ships an optional AT-SPI2
bridge for Wayland desktops. The model is the contract: widgets derive a role,
name, description, state, text and value from their kind and content, and the
application can override the derived values. The bridge is one consumer of that
model; another platform adapter can be built from the same getters and events
without changing widgets.

The model lives in [`pt/a11y.h`](../include/pt/a11y.h). The AT-SPI bridge is in
`src/a11y_bridge.c` and is enabled per app.

## Enabling the bridge

```c
pt_app *app = pt_app_create("dev.example.notes");
if (!pt_app_enable_a11y(app)) {
    /* No D-Bus build, no session bus or no accessibility bus. The app still
     * runs; report the status if the UI has a diagnostics page. */
}

pt_a11y_status status = pt_app_get_a11y_status(app);
/* pt_a11y_status_disabled, _unavailable or _active */
```

`pt_app_enable_a11y` returns `false` for a `NULL` app, a build without D-Bus, or
an unreachable accessibility bus. It is safe to keep running. Enabling is
opt-in so the core has no hard runtime dependency on a daemon;
`pt_app_disable_a11y` closes the bridge. The event callback is independent of
the bridge and stays dormant until installed:

```c
pt_app_set_a11y_callback(app, on_a11y_event, state);
```

The callback runs synchronously on the UI thread. It must not block. The same
event is forwarded to the AT-SPI bridge when the bridge is active, so a custom
adapter and the built-in bridge can coexist.

## Roles

A widget with an explicit role uses it. Otherwise the role is derived from the
widget kind. The table shows the Pt role and what the AT-SPI bridge publishes.
"Override" means the role is normally set by an overlay or a custom widget, not
by the kind switch.

| Widget kind | Default `pt_a11y_role` | AT-SPI role |
| --- | --- | --- |
| `pt_widget_label` | `label` | label |
| `pt_widget_button` | `push_button` | button |
| button inside a menu | `menu_item` | menu item |
| `pt_widget_entry` | `text_frame` | entry |
| `pt_widget_text_area` | `text_frame` | entry |
| `pt_widget_list` | `list` | list |
| `pt_widget_list_row` | `list_item` | list item |
| `pt_widget_toggle` | `switch` | toggle button |
| `pt_widget_slider` | `slider` | slider |
| `pt_widget_scroll` | `scroll_pane` | scroll pane |
| `pt_widget_divider` | `separator` | separator |
| `pt_widget_card` | `panel` | panel |
| `pt_widget_spacer` | `none` | unknown |
| `pt_widget_checkbox` | `check_box` | check box |
| `pt_widget_radio` | `radio_button` | radio button |
| `pt_widget_progress` | `progress_bar` | progress bar |
| `pt_widget_spinner` | `spinner` | progress bar |
| `pt_widget_segmented` | `group` | grouping |
| `pt_widget_icon` | `icon` | icon |
| `pt_widget_table` | `table` | table |
| `pt_widget_table_header` | `generic` | filler |
| `pt_widget_table_header_cell` | `column_header` | table column header |
| `pt_widget_table_row` | `table_row` | table row |
| `pt_widget_table_cell` | `table_cell` | table cell |
| `pt_widget_tree` | `tree` | tree |
| `pt_widget_tree_row` | `tree_item` | tree item |
| `pt_widget_tabs` | `tab_list` | page tab list |
| `pt_widget_tab_page` | `tab_panel` | panel |
| `pt_widget_sidebar` | `list` | list |
| `pt_widget_sidebar_item` | `list_item` | list item |
| `pt_widget_combobox` | `combo_box` | combo box |
| `pt_widget_box` and default | `generic` | filler |
| menu overlay | `menu` (override) | popup menu |
| dialog overlay | `dialog` (override) | dialog |
| toast overlay | `alert` (override) | alert |
| tooltip overlay | `tooltip` (override) | tool tip |

The combobox popup panel and its scroll wrapper are set to `list_box` by the
combobox implementation, which AT-SPI publishes as a list box rather than as a
plain list. Menu items and overlay panels carry explicit roles because they are
built from ordinary buttons and cards.

`pt_widget_set_a11y_role(widget, role)` replaces the derived role for one
widget. Passing `pt_a11y_role_none` restores derivation. A role change emits a
structure event. Use a role override when a custom widget's painted meaning is
not captured by its kind, not to make a button pretend to be a checkbox.

## States

`pt_widget_get_a11y_state` returns a mask of `pt_a11y_state_*` bits. Visible and
enabled are inherited: a widget is visible only when it and every ancestor are
visible, and enabled only when it and every ancestor are enabled.

| Pt state | Derivation |
| --- | --- |
| `visible` | widget and all ancestors visible |
| `enabled` | widget and all ancestors enabled |
| `focusable` | `pt_widget_set_focusable` is true |
| `focused` | widget currently owns keyboard focus |
| `checked` | toggle, checkbox or radio is checked |
| `selected` | row is in the selection; segmented control is selected; active tab page is selected |
| `editable` | entry or text area is enabled |
| `busy` | spinner is always busy; progress is busy while indeterminate |
| `expanded` | tree node is open; combobox popup is open |
| `expandable` | tree node has children; combobox always |
| `selectable` | enabled data-view row, sidebar item or tab page |
| `multi_selectable` | list, table or tree container |
| `modal` | widget's explicit role is `dialog` |

The AT-SPI bridge adds a small number of states that are not separate Pt bits:

- visible also sets `SHOWING`; enabled also sets `SENSITIVE`; focused also sets
  `ACTIVE` on both the widget and its window.
- toggle, checkbox and radio report `CHECKABLE` and `SELECTABLE` in addition to
  `CHECKED` when checked. A segmented control reports `SELECTABLE`.
- an indeterminate progress bar reports `INDETERMINATE`; a non-empty tooltip
  reports `HAS_TOOLTIP`; a pressed widget reports `PRESSED`.
- an entry reports `SELECTABLE_TEXT`; a label, button or text area that is not
  an entry reports `READ_ONLY`.
- an expandable tree item that is not expanded reports `COLLAPSED`, so a screen
  reader announces the closed state rather than staying silent.

Note that `selectable` is separate from `selected`: every enabled row of a
multi-selectable view is selectable, and only the rows in the set are selected.
This is what lets a screen reader offer "add to selection" on an unselected row.

## Names, descriptions, text and values

`pt_widget_get_a11y_name` derives a name in this order:

1. the application override from `pt_widget_set_a11y_name`;
2. the tree node's text for a tree row;
3. the widget's own text, when non-empty;
4. the sidebar header for a sidebar;
5. the placeholder for an entry or text area;
6. the empty string.

A description has no derivation: it is the override from
`pt_widget_set_a11y_description`, or the empty string. Setting `NULL` or an
empty string clears either override and restores the derived value. Both setters
emit a name-changed event.

`pt_widget_get_a11y_text` returns borrowed text plus UTF-8 **byte** offsets:

- labels and buttons return their text with caret and selection offsets zero;
- entries and text areas return text, caret, selection start and selection end;
- a password entry returns `false`; the AT-SPI bridge still publishes the
  password role and caret position but returns an empty string for text
  queries. The character count reflects the real length, so an adapter should
  not treat it as a content leak unless the application considers length
  sensitive;
- a widget with no text returns `false`.

`pt_widget_get_a11y_value` returns a float for sliders and progress bars
(`[0, 1]`), toggles, checkboxes and radios (`0` or `1`), and segmented controls
(the selected index). Other kinds return `false`.

## Overrides in practice

```c
pt_widget *save = pt_button_create(form, "Save");
pt_widget_set_a11y_name(save, "Save note");
pt_widget_set_a11y_description(save, "Writes the current note to disk");

pt_widget *details = pt_card_create(form);
pt_widget_set_a11y_role(details, pt_a11y_role_group);
pt_widget_set_a11y_name(details, "Note details");

pt_widget *search = pt_entry_create(toolbar, "Search");
pt_widget_set_a11y_name(search, "Search notes");
```

Overrides are copied. They survive widget restyles and theme changes, and are
released with the widget. A reconstructed widget loses its overrides, so apply
them in the code that builds the widget rather than once at startup.

The AT-SPI bridge derives an accessible name for a window from its title and
publishes the application name from `pt_app_create`. It also publishes
`label-for`/`labelled-by` relations for table headers and cells,
`member-of`/`node-child-of`/`node-parent-of` relations for data views and tree
nodes, `subwindow-of` for windows and `popup-for` for popup surfaces.

## Events

`pt_a11y_event_type` is the platform-neutral event stream:

| Event | When it fires | AT-SPI signal |
| --- | --- | --- |
| `focus_changed` | focus enters or leaves a widget | `Focus` plus a `focused` state change |
| `state_changed` | enabled, checked, busy or similar state changes | `StateChanged` for the affected state |
| `text_changed` | editable text or caret changes | `TextChanged` with the new text |
| `selection_changed` | a data-view selection moves | `SelectionChanged` plus a `selected` state change for the primary row |
| `value_changed` | slider, progress or segmented value changes | `PropertyChange` for `accessible-value` |
| `name_changed` | name or description override changes | `PropertyChange` for `accessible-name` |
| `structure_changed` | role override, overlay open/close or child order changes | `ChildrenChanged` with `reorder` |

Toasts use the AT-SPI announcement signal with the politeness advertised by the
toast (polite or assertive). A focus change is emitted for the widget that
gained focus; the window's focused state is read from the application object
and the window's own `Component` state. Events are synchronous and carry the
widget's current state, so an adapter does not need to poll after receiving
one.

## What the AT-SPI bridge publishes

Every object implements `org.a11y.atspi.Accessible`. Windows and widgets
implement `Component`; the application root is the only object that implements
`Application`. The rest depend on the widget:

| Interface | Published for |
| --- | --- |
| `Accessible` | every app, window and widget |
| `Application` | app root only |
| `Component` | every window and widget |
| `Action` | enabled actionable widgets (`click`), and focusable non-actionable widgets (`focus`) |
| `Text` | entries, text areas, labels and buttons |
| `EditableText` | entries and text areas (write methods return false when disabled) |
| `Value` | sliders, progress bars, toggles, checkboxes, radios and segmented controls |
| `Table` | tables |
| `TableCell` | table cells (not header cells) |
| `Selection` | lists, tables, trees, sidebars and tab strips |

Representative methods:

- **Accessible:** `GetChildCount`, `GetChildAtIndex`, `GetChildren`,
  `GetIndexInParent`, `GetRole`, `GetRoleName`, `GetState`, `GetAttributes`,
  `GetRelationSet`, `GetInterfaces`, `GetApplication`, name and description
  queries.
- **Component:** `Contains`, `GetAccessibleAtPoint`, `GetExtents`,
  `GetPosition`, `GetSize`, `GetLayer`, `GrabFocus`, `ScrollTo`. Mutating
  extents, position and size return `false`.
- **Action:** `GetActions` and `DoAction`; index 0 is `click` on an actionable
  widget and `focus` otherwise.
- **Text:** `GetText`, `GetCharacterCount`, `GetStringAtOffset`,
  `GetTextBeforeOffset`/`AtOffset`/`AfterOffset`, `GetCharacterAtOffset`,
  `GetAttributeValue`, `GetCharacterExtents`, `GetOffsetAtPoint`,
  `GetNSelections`, `GetSelection`, `AddSelection`, `SetSelection`,
  `RemoveSelection`, `SetCaretOffset`. Offsets are Unicode character offsets
  over D-Bus, converted to UTF-8 bytes internally.
- **EditableText:** `SetTextContents`, `InsertText`, `DeleteText`, `CopyText`,
  `CutText`, `PasteText`. They return `false` for a disabled text input.
- **Value:** `GetMinimumValue`, `GetMaximumValue`, `GetMinimumIncrement`,
  `GetCurrentValue`, `SetCurrentValue`.
- **Table:** row/column counts, `GetAccessibleAt`, `GetIndexAt`,
  `GetRowAtIndex`/`GetColumnAtIndex`, row/column descriptions, extents,
  headers, selection queries and row/column selection.
- **TableCell:** row/column indices, extents, spans and header cells.
- **Selection:** `GetSelectedChild`, `SelectChild`, `DeselectChild`,
  `DeselectSelectedChild`, `IsChildSelected`, `SelectAll`, `ClearSelection`.

The bridge skips unbound row widgets from the virtualized pools, so a
10,000-row table exposes only the rows that currently exist as widgets. Overlay
panels are appended after the window root in the accessibility tree, matching
their paint order. Trees expose visible nodes; a collapsed branch is not
enumerated until it is expanded.

## Building a different adapter

A non-AT-SPI adapter needs to implement the same conceptual surface. The
minimum contract is:

1. **Tree.** Start at each window, then the root widget, then real children.
   Include open overlay panels after the root, skip unbound row placeholders,
   and resolve tree rows to their nodes. Widget pointers are valid while the
   widget is owned by a live window; a structure event tells you to re-read the
   child list.
2. **Semantics.** Use the public getters for role, name, description, state,
   text and value rather than reading widget internals. Apply the same
   visible/enabled ancestor rules, because the getters already do.
3. **Actions.** The public API exposes focus (`pt_widget_focus`) but no generic
   activation entry point. For activation parity, route platform commands into
   the application's own callbacks, or enable the AT-SPI bridge, whose
   `DoAction` uses the internal activation path.
4. **Text edits.** Use the public text setters and
   `pt_entry_set_selection` / `pt_text_area_set_selection` for caret and
   selection. Do not call the model with byte offsets where the platform
   expects characters; convert at the boundary.
5. **Events.** Install `pt_app_set_a11y_callback` and forward the seven event
   types. Do not block in the callback. Re-read state on `state_changed` and
   re-read the child list on `structure_changed`.
6. **Threading.** Every getter and setter is UI-thread only, including the
   getters. An adapter that receives a request on another thread must post work
   to the UI thread itself; Pt 1.0 has no `pt_app_invoke`.
7. **Lifetime.** Handles are borrowed. Do not free widgets, and do not retain a
   widget pointer across a structure change without revalidating it. The bridge
   keeps its own IDs and object paths for this reason.

## Testing

- `tests/test_a11y.c` covers role and state derivation, overrides, text and
  value exposure, password behavior, and event delivery headlessly.
- `tests/test_a11y_bridge.c` starts the real D-Bus bridge when a session bus is
  available and asserts interfaces, roles, states, names, text, values,
  actions, relations and selection operations. The text assertions cover a
  read-only label, a selectable and editable multi-line text area with UTF-8
  offsets, and a password entry whose text and character code stay hidden while
  its character count remains available. It skips cleanly when no bus is
  available.
- `examples/c/accessibility.c` is a runnable form with names, descriptions,
  roles, a live region and a status readout; `make run-a11y` starts it.
- To test a custom adapter, build a headless app (`PT_HEADLESS=1`), install the
  event callback, mutate the UI through public setters, and assert the events
  and getters. A real AT-SPI session additionally needs `dbus-daemon` and an
  accessibility bus; `tools/release-check.sh` reports the bridge as a live
  check rather than as part of `make check`.

## Limitations in 1.0

- The bridge is opt-in and requires a build with `dbus-1`. Without a reachable
  accessibility bus, `pt_app_enable_a11y` returns `false` and the app runs
  without assistive-technology exposure.
- The model describes widgets, not arbitrary painted content. A custom widget
  that draws its own controls must expose a role, name and state itself; the
  bridge cannot infer meaning from pixels.
- There is no public activation function. `pt_widget_focus` is public; action
  dispatch is an AT-SPI bridge feature.
- Password entries hide their text but report their caret and character count.
  An application that treats password length as sensitive should account for
  that in its threat model.
- Bidi paragraph layout is post-1.0. Text offsets are byte offsets in the
  stored UTF-8 string and character offsets over D-Bus; a future bidi
  implementation must keep both logical, with a separate visual map for caret
  and hit testing. The [RTL plan](rtl.md) makes that requirement explicit,
  including the AT-SPI direction attribute and password checks.
- The bridge publishes `TableCell` for body cells, not for header cells.
  Header cells appear through `Table` header queries and the `column_header`
  role, not as `TableCell` implementations.
- There is no accessibility tree diff protocol beyond the emitted events. An
  adapter that misses an event must re-enumerate the affected subtree.
