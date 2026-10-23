# iOS / Android calendar plan

## iPhone / iPad

Apple Calendar can add a CalDAV account. HomePanel is structured to expose one
CalDAV principal per e-mail-linked family person.

Example logical account:
- server: `homepanel.local`
- username: family member e-mail
- password: local HomePanel CalDAV password

The final transport should use HTTPS before production use.

## Android

The same CalDAV model can be used by DAV-compatible Android clients.
Google calendars remain a separate external provider path.

## Google

Google's current CalDAV interface requires:
- HTTPS
- OAuth 2.0
- current CalDAV endpoint format

The primary Google calendar ID is normally the user's e-mail address.

HomePanel therefore keeps `accountEmail`, provider, remote principal and sync token
separate from the local person record.
