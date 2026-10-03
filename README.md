# Windows hardware identity inventory

`hwid.exe` is a native C++20, read-only Windows 10/11 x64 console tool.
It inventories what Windows and device drivers expose, with provenance,
confidence, mutability, availability, and per-field diagnostics. It does not
compute a combined machine fingerprint or claim that every value is unique.

## Build

Install Visual Studio 2022 or newer with **Desktop development with C++**, a
Windows 10/11 SDK, CMake 3.25 or newer, and Ninja. In an **x64 Native Tools
Command Prompt** or an initialized **Developer PowerShell**:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64
ctest --preset windows-x64
.\build\windows-x64\hwid.exe --help
```

The preset builds Release with tests enabled. No packages are downloaded and
no third-party build dependencies are required. Alternatively, use a Visual
Studio generator (no Ninja required):

```powershell
cmake -S . -B build/vs2022 -G "Visual Studio 17 2022" -A x64
cmake --build build/vs2022 --config Release
ctest --test-dir build/vs2022 -C Release --output-on-failure
.\build\vs2022\Release\hwid.exe --json --redact
```

Use the generator matching your Visual Studio version. CMake's install target
can copy the executable to a chosen destination:

```powershell
cmake --install build/windows-x64 --prefix out
```

If multiple toolchains are installed, initialize the MSVC x64 environment
before configuring. An incomplete developer environment can select an unrelated
MinGW linker. After fixing the environment, use `cmake --fresh --preset windows-x64`.
The executable uses the MSVC runtime; other machines need the matching Microsoft
Visual C++ x64 Redistributable.

## Usage

```powershell
hwid.exe
hwid.exe --json
hwid.exe --verbose --redact
hwid.exe --component motherboard
hwid.exe --component storage --json
hwid.exe --tpm-info
```

Components: `system`, `motherboard`, `bios`, `tpm`, `storage`, `network`,
`usb`, `gpu`, `sdEmmc` (also `sd-emmc`), and `cpu`.
`--component` selects one collector; it does not run the other collectors.
`--tpm-info` is an alias for `--component tpm`.

Each collector also has a direct switch: `--system`, `--motherboard`, `--bios`,
`--tpm`, `--storage`, `--network`, `--usb`, `--gpu`, `--sd-emmc`, and `--cpu`.
For example, `hwid.exe --storage --json` runs only the storage collector.
Use one collector switch at a time; omitting the filter runs all ten.
Direct switches work with `--json`, `--verbose`, and `--redact`.

By default, stdout contains tables grouped by component and device, with
`Identifier`, `Value`, and `Status` columns. Values are not truncated.
Use `--verbose` to include provenance, confidence, mutability, raw values and
diagnostics below each table. `--json` switches stdout to machine-readable JSON.

Exit codes: **0** means the inventory completed, including partial failures;
**1** means a fatal application/output error; **2** means invalid arguments.
Inspect each result's `available`, `status`, and `error` for device failures.
The application never requests elevation or changes device/TPM configuration.

## Implemented collectors

| Category | Data and source | Limits |
| --- | --- | --- |
| System | WMI `Win32_ComputerSystemProduct.UUID`, vendor, name | Rejects malformed, zero and FF UUIDs; virtual/OEM firmware can change them |
| Motherboard | All WMI `Win32_BaseBoard` rows: manufacturer, product, serial | Retains and flags OEM placeholders |
| BIOS | WMI `Win32_BIOS` manufacturer, version, serial | Labels the BIOS-reported system/service serial without claiming a ROM-chip ID |
| Storage | SetupAPI disk interfaces, `IOCTL_STORAGE_QUERY_PROPERTY` descriptor, physical drive number where available | Serial is storage-stack reported; RAID/USB/virtual layers can substitute or hide it |
| Network | IP Helper adapters; current MAC and `GetIfEntry2` permanent MAC, type/status/hardware flag | Includes virtual interfaces; permanent MAC remains driver-reported |
| USB | Present USB devnodes, VID/PID, manufacturer, instance ID, unique-ID capability | Labels serial inferred from physical USB instance component; does not read iSerialNumber directly |
| TPM | TBS presence/version; WMI state and `IsReady`; CNG public EK/certificate blob SHA-256 | Readiness may require admin access; failures remain explicit; no certificate trust or attestation verification |
| GPU | All DXGI adapters: PCI model IDs, revision, local LUID, software flag; optional NVML UUID and board serial | DXGI LUID is transient; NVML enumeration is separate and not assumed to match DXGI ordering |
| SD/eMMC | Storage devices reporting SD/MMC bus types, storage-stack serial | CID explicitly unsupported in this version; USB readers may hide the card's bus type |
| CPU | CPUID vendor/brand/family/model/stepping/features; WMI processors | CPUID describes the current logical processor; modern x86/x64 unique serial explicitly unavailable |

NVIDIA NVML loads dynamically from the Windows system directory using a
restricted DLL search. Missing DLLs or unsupported functions do not abort the
scan. AMD/Intel vendor UUID backends are explicitly reported as unimplemented;
their adapters still receive DXGI inventory.

Protocol-specific NVMe queries, controller-specific SD CID paths, AMD/Intel
vendor identity backends, TPM key enrollment, and attestation are future
extensions from the plan. `--create-device-key` and `--tpm-attestation` are not
implemented; unknown options fail rather than silently doing something else.
Passive TPM inventory does not create persisted keys or export private keys.

## Results and privacy

JSON has `schemaVersion: 1`, a `redacted` flag, and ten component arrays.
Unselected component arrays are empty. Each record includes:

```json
{
  "component": "storage",
  "deviceName": "PhysicalDrive0",
  "identifierType": "Storage serial",
  "rawValue": "  EXAMPLE  ",
  "normalizedValue": "EXAMPLE",
  "source": "IOCTL_STORAGE_QUERY_PROPERTY/STORAGE_DEVICE_DESCRIPTOR.SerialNumberOffset",
  "confidence": "InterfaceAssigned",
  "status": "available",
  "potentiallyMutable": true,
  "available": true,
  "error": "Storage-stack-reported serial; bridge, RAID or virtual controller may substitute it. Not an NVMe namespace identifier."
}
```

The `error` field may also contain an explanatory caveat on an available result.
Statuses distinguish missing devices, access denial, unsupported operations,
unexposed fields, placeholders, and query failures. Numeric API codes and
HRESULTs are preserved in diagnostics. An empty enumeration produces a
not-present result. Failure in one collector does not prevent subsequent ones.

Normalization trims boundary whitespace/NULs without removing internal spaces.
UUIDs become lowercase and MACs uppercase colon-separated hex. Raw values are
retained, including placeholders. TPM output intentionally contains only hashes
of public provider blobs, not certificate contents; those hashes are still
persistent identifiers and are not an authentication proof.

Nothing is automatically transmitted or saved to disk. Both normal output and
`--json` can expose persistent identifiers. `--redact` masks **all** values,
device names, and diagnostics in both writers, including verbose raw values.
Structural metadata and statuses remain visible. Output is UTF-8, with JSON
escaping and escaped control characters in console strings. To save a report,
explicitly redirect stdout yourself.

## Validation

CTest includes normalization and malformed descriptor fixtures, Unicode/JSON
escaping, invalid placeholders, error isolation and access-denied classification,
missing devices, multiple records, redaction, CLI parsing, and a live inventory
smoke test. Tests use explicit checks in Release builds, not disabled `assert`s.
The integration script parses JSON with PowerShell and never logs raw IDs.

Local validation: MSVC 19.44 x64 Release with Windows SDK 10.0.26100.0 built
without warnings; all four CTest tests passed (35 unit/fixture checks). The
live scan ran without elevation and returned usable data for system,
motherboard, BIOS, TPM, storage, network, USB, GPU, and CPU. This host did not
expose an SD/MMC disk. Restricted and unsupported fields remained explicit.

Validation on this development host does not establish the entire hardware
matrix in the plan. Repeat `ctest` on Windows 10/11 physical machines and a
Hyper-V/VMware VM, as standard user, and check NVMe/SATA/RAID, USB bridges,
multiple NICs/GPUs, SD readers, absent/disabled TPM, and optional NVIDIA hardware.
No unavailable hardware is emulated as a successful identifier.

## API references

- [Microsoft storage descriptor](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ns-winioctl-storage_device_descriptor)
- [Microsoft GetIfEntry2](https://learn.microsoft.com/en-us/windows/win32/api/netioapi/nf-netioapi-getifentry2)
- [Microsoft USB serial and unique-ID capability](https://devblogs.microsoft.com/windows-music-dev/the-importance-of-including-a-unique-iserialnumber-in-your-usb-midi-devices/)
- [Microsoft TPM device information](https://learn.microsoft.com/en-us/windows/win32/api/tbs/nf-tbs-tbsi_getdeviceinfo)
- [Microsoft TPM readiness and access requirements](https://learn.microsoft.com/en-us/windows/win32/secprov/win32-tpm-isready)
- [NVIDIA NVML reference](https://docs.nvidia.com/deploy/nvml-api/index.html)
