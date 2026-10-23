# Contact form and shared calendar functions

The lazy account connector belongs inside the contact detail panel, not as a
third sibling of the two-column workspace. That misplaced grid child put the
editor in the narrow list column. The editor now retains its intended width.

Contact editing immediately shows one phone, email and address entry, with
additional entries still optional. Address data is not a collapsed section.
Value inputs precede explicitly labelled type/category fields and have suitable
autocomplete hints. Before saving the current DOM values are copied to a fresh
payload, including browser-autofilled inputs that did not dispatch input events.
An email accidentally entered in an otherwise empty category field is recovered
as the email value; other incomplete non-default categories produce an error,
not silent data loss. Unused blank placeholders are not persisted.

Birthdays format incrementally as YYYY-MM-DD or --MM-DD, preserve caret position
and are normalized again before saving. Existing central date validation remains.
Structured email/phone/address arrays are authoritative on reading; legacy primary fields
are synchronized for list previews and removed entries no longer resurface.

Birthdays and regional holidays are shared functions, with separate global
visibility switches in native and browser settings. Their existing internal
virtual record IDs remain intact for stored data/API compatibility; they are not
ordinary selectable calendars in the native controller or browser user/filter UI.
They do not consume any of the four personal calendar slots. Older selections
containing virtual IDs are filtered on read/save without deleting user data.
Enabled shared functions bypass person, personal/family preset and ordinary
calendar filters, so everyone sees the same birthdays and holidays. Disabling
one remains a central setting affecting all users. No additional tasks/provider
or event store was introduced.
