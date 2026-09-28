<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
### session · build — project import uses the TOML storage contract

Pin felitronics-toml v0.3.0, including the embedding tool. Project import declares the library's allocation-free
parse/read allowance for its text and required paths, plus its own inline storage's zero heap demand. Checked sums
refuse an unrepresentable demand. The session's parser-size estimate and separate 16 KiB text cap are gone;
felitronics-toml enforces its own document limit.

The session budget law continues to measure the whole import, with realistic tightness and adversarial project
measurements on every tier. Windows MSVC Debug also runs the event, project and replay suites, and its expected-name
gate requires all thirteen suites.
