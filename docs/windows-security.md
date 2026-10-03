# Windows security and release signing

FrameSnap's current GitHub Actions ZIPs are **unsigned development builds**. There is no trusted signed release yet. Native Windows build, lifecycle, and installer tests pass, but those runners do not reproduce every user's Application Control policy. Those tests are not an antivirus verdict or evidence of publisher trust.

## "An Application Control policy has blocked this file"

This error means Windows refused to start the executable. It can come from Smart App Control or another application control policy. The message alone does not identify the policy or establish that malware was detected.

During installation, FrameSnap first launches the replacement executable with `--quit` to check that it can start and let an existing instance finish saving. If Windows blocks that launch, installation stops **before changing installation files, shortcuts, startup registration, or the Installed apps entry**. The installer does not disable protection or ignore the failed launch. Skipping it would leave an installation that still cannot capture.

To identify the policy:

1. Open **Windows Security > App & browser control > Smart App Control settings** and note whether it says **On**, **Evaluation**, or **Off**. Do not change it as a troubleshooting step. Smart App Control enforces blocks when On; Evaluation and Off do not explain an enforced Smart App Control block.
2. In **Event Viewer > Applications and Services Logs > Microsoft > Windows > CodeIntegrity > Operational**, find the event for `FrameSnap.exe` at the time it failed. Event **3077** records an enforced code integrity block; **3076** records an audit event. Record the file and policy name/ID from the event. Other application control mechanisms can use different logs; absence of these events does not prove the app is allowed.
3. For a work/school-managed device, give the event details to the administrator. A valid signature alone may not satisfy an organization's allowlist.

For Smart App Control, an unknown unsigned binary can be blocked even when it is legitimate. FrameSnap needs a release signed by a trusted publisher, followed by testing under Smart App Control. Redownloading the same unsigned build, running as administrator, or bypassing PowerShell execution policy does not supply that trust. A normal SmartScreen "Windows protected your PC" warning is a different UI; do not assume its override steps apply to an Application Control block.

If Windows instead reports a specific malware/threat name, retain that name and the file hash for investigation. Do not assume it is a false positive or disable Windows Security.

## Release signing

The missing prerequisite is access to a verified publisher's trusted code-signing identity. A self-signed certificate does not supply public publisher trust. Do not install a new root certificate on users' devices to make a development build appear trusted.

For a distributable release:

1. Obtain a code-signing identity from a trusted provider or configure Microsoft's managed signing service. Smart App Control's documented signature check requires **RSA**, not ECC. Identity verification and access to the signing key/service must be provided by the publisher; this repository does not include them.
2. Sign the final `FrameSnap.exe` and the PowerShell install/uninstall scripts before packaging. Use SHA-256 and a trusted timestamp. Keep private keys in the provider's supported secure storage, outside the repository and logs. Verify the signatures on the extracted package as well as before packaging.
3. Build the ZIP and compute its checksum **after** signing. A checksum or GitHub artifact digest is not a substitute for Authenticode publisher verification.
4. Test the actual downloaded and extracted package on Windows with Smart App Control On, including install, capture, upgrade, and uninstall. Review code integrity events for all executables and scripts. Test organization-managed deployments against their actual policy separately.
5. Publish the tested signed package through the intended release channel. Keep unsigned CI artifacts clearly labeled. Signing does not promise immediate SmartScreen reputation or approval under every enterprise policy.

## Microsoft references

- [Smart App Control overview and enforcement modes](https://learn.microsoft.com/windows/apps/develop/smart-app-control/overview)
- [Signing requirements and trusted providers](https://learn.microsoft.com/windows/apps/develop/smart-app-control/code-signing-for-smart-app-control)
- [Testing signatures and checking CodeIntegrity events](https://learn.microsoft.com/windows/apps/develop/smart-app-control/test-your-app-with-smart-app-control)
