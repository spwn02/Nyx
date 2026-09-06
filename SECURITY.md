# Security Policy

## Supported versions

Nyx is pre-1.0. Security fixes are maintained for:

- the current `master` branch; and
- the most recent published release when a release exists.

Older development snapshots and superseded prereleases may not receive fixes.

## Reporting a vulnerability

Do not report suspected vulnerabilities in a public issue, discussion, or pull request.

Use GitHub private vulnerability reporting for `spwn02/Nyx` when it is available. If private vulnerability reporting is not enabled, contact the repository owner through a private contact method published on the maintainer's GitHub profile before disclosing details publicly.

Include enough information to reproduce and assess the issue:

- affected revision or release;
- affected platform/toolchain;
- impact;
- reproduction steps or proof of concept;
- whether the issue is already public;
- any suggested mitigation.

Avoid including secrets, credentials, personal data, or unrelated production data in a report.

## Scope

Reports are especially useful for vulnerabilities involving:

- unsafe parsing or memory handling;
- asset/shader ingestion;
- filesystem or process boundaries;
- package/release integrity;
- dependency or submodule provenance;
- misuse of the reference toolchain that can compromise generated binaries.

If a vulnerability originates entirely in Miracle, Switch, clang-cxx26, or a vcpkg dependency, report it to that upstream project as well. If the issue is exploitable through Nyx, reporting it here too is appropriate.

## Disclosure

Please allow a reasonable period for investigation and remediation before public disclosure. Coordinated disclosure details can be agreed on after the report is acknowledged.

No bug bounty or guaranteed response-time SLA is currently offered.
