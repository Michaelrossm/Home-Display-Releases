# Web access and browser standby

The existing main HTTPS server permits six client sockets (previously two),
intended to provide headroom for three browsers. The optional management server
and HTTP-to-HTTPS redirect remain separate, unchanged services. The global lwIP
budget is 24 sockets, including the servers' internal sockets, outgoing network
jobs and other firmware services. Idle clients remain reclaimable by HTTPD LRU.
Receive/send timeouts stay bounded at five seconds.

mbedTLS dynamic TX/RX buffers return unused record buffers to the internal heap.
Private-key/config data is deliberately not freed after handshake: reconnects
must work, and TLS memory remains internal. No cipher, certificate, permission,
CSRF, credential storage or session lifetime setting is weakened. Existing
generated sdkconfig files must contain the new defaults when compiling.

The existing hpWeb lifecycle is shared by the dashboard, app settings, contacts,
calendar, intercom and account portal. Each tab serializes at most 16 outstanding
requests. Hidden/page-hidden tabs and visible tabs without pointer/key/wheel
activity for 60 seconds stop optional refresh timers and cancel in-flight reads.
Queued requests from before standby are discarded, including unsent writes.
Already submitted writes may still complete and are never automatically replayed.
Explicit uploads/firmware transfers are not silently cancelled as background reads.

Returning to a tab, focusing it or interacting after inactivity resumes bounded
read refreshes. Calendar resumes only the agenda, not person/account editors.
Phonebook refreshes the list without replacing the contact draft. App-settings
refreshes use the existing preserveAppSettings path. Cookies and local drafts
are not cleared. Provider sign-in remains the existing account service; the
account portal no longer uses an always-running setInterval loop.

No new firmware task, per-app server, timer task or parallel network engine is
created. Browser standby does not switch off the device's shared HTTPS listener
or independent native calendar/printer background services. It avoids optional
browser work; it does not promise zero device power consumption.

Node runtime tests cover three concurrent browser contexts with independent
request serialization, standby abort/discard behavior, no replay of submitted
writes, inactivity/resume, bounded polling and retained local draft state.
The native route-start fixture checks TLS socket capacity, LRU and timeouts.
These checks are not a three-physical-device hardware load test.
