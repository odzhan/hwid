# Windows C++ Hardware Identity Reader --- Implementation Plan

## 1. Objective

Build a native Windows console application in modern C++ that
inventories hardware identifiers exposed by Windows, device firmware, or
vendor APIs.

The program should collect identifiers from:

-   Computer system / SMBIOS system UUID
-   Motherboard serial number
-   BIOS serial number
-   TPM 2.0 identity information
-   NVMe SSD serial numbers
-   SATA SSD/HDD serial numbers
-   Network adapter permanent MAC addresses
-   USB device serial numbers
-   GPU identifiers / UUIDs where available
-   SD/eMMC CID or equivalent identifiers where Windows exposes them
-   CPU identification information, while explicitly reporting that a
    generally usable unique CPU serial number is not available on modern
    x86/x64 Windows PCs

The application should **not claim that every returned value is globally
unique**. Each result should include its provenance and reliability.

------------------------------------------------------------------------

## 2. Recommended Target

### Language and toolchain

-   C++20
-   Visual Studio 2022 or newer
-   MSVC x64 build
-   Windows 10/11
-   CMake for the build system
-   Unicode Windows APIs (`W` variants)
-   RAII wrappers for handles, COM objects, and allocated buffers

### Initial executable

``` text
hwid.exe
```

Example usage:

``` text
hwid.exe
hwid.exe --json
hwid.exe --verbose
hwid.exe --component motherboard
hwid.exe --component storage
hwid.exe --component tpm
```

------------------------------------------------------------------------

## 3. Design Principles

1.  **Read only.** Do not modify firmware, device configuration,
    registry identifiers, TPM state, or network configuration.
2.  **Prefer documented Windows APIs.** Use WMI/CIM when Windows does
    not provide a simpler documented API.
3.  **Keep raw and normalized values separate.** Preserve the value
    returned by Windows and create a second normalized representation
    for comparison.
4.  **Record provenance.** Every identifier must say where it came from.
5.  **Treat absence as normal.** OEMs and device manufacturers may omit
    identifiers.
6.  **Never silently substitute identifiers.** A missing motherboard
    serial must remain missing rather than being replaced with another
    field.
7.  **Do not call an identifier unique unless its semantics justify that
    claim.**
8.  **Support multiple devices.** A PC can have multiple disks, NICs,
    GPUs, and USB devices.
9.  **Avoid unnecessary administrator requirements.** Run as a standard
    user by default and clearly report access-denied cases.
10. **Separate collection from policy.** Hardware collection should not
    itself decide whether two machines are the same machine.

------------------------------------------------------------------------

## 4. Result Model

Use one common structure for all collectors.

``` cpp
enum class IdConfidence
{
    HardwareBacked,
    ManufacturerAssigned,
    FirmwareAssigned,
    InterfaceAssigned,
    Informational,
    Unknown
};

struct HardwareIdentifier
{
    std::string component;
    std::string deviceName;
    std::string identifierType;
    std::string rawValue;
    std::string normalizedValue;
    std::string source;
    IdConfidence confidence;
    bool potentiallyMutable;
    bool available;
    std::string error;
};
```

A collector should return:

``` cpp
using HardwareResults = std::vector<HardwareIdentifier>;
```

This permits multiple disks, adapters, GPUs, and USB devices without
forcing them into a single-machine structure.

------------------------------------------------------------------------

## 5. Proposed Source Layout

``` text
hwid/
├── CMakeLists.txt
├── README.md
├── src/
│   ├── main.cpp
│   ├── app/
│   │   ├── Cli.cpp
│   │   └── Cli.h
│   ├── common/
│   │   ├── HardwareIdentifier.h
│   │   ├── Error.cpp
│   │   ├── Error.h
│   │   ├── Strings.cpp
│   │   ├── Strings.h
│   │   ├── WinHandle.h
│   │   └── ComPtrHelpers.h
│   ├── collectors/
│   │   ├── SystemCollector.cpp
│   │   ├── SystemCollector.h
│   │   ├── MotherboardCollector.cpp
│   │   ├── MotherboardCollector.h
│   │   ├── BiosCollector.cpp
│   │   ├── BiosCollector.h
│   │   ├── TpmCollector.cpp
│   │   ├── TpmCollector.h
│   │   ├── StorageCollector.cpp
│   │   ├── StorageCollector.h
│   │   ├── NetworkCollector.cpp
│   │   ├── NetworkCollector.h
│   │   ├── UsbCollector.cpp
│   │   ├── UsbCollector.h
│   │   ├── GpuCollector.cpp
│   │   ├── GpuCollector.h
│   │   ├── SdEmmcCollector.cpp
│   │   ├── SdEmmcCollector.h
│   │   ├── CpuCollector.cpp
│   │   └── CpuCollector.h
│   ├── windows/
│   │   ├── WmiClient.cpp
│   │   ├── WmiClient.h
│   │   ├── DeviceEnumerator.cpp
│   │   └── DeviceEnumerator.h
│   └── output/
│       ├── ConsoleWriter.cpp
│       ├── ConsoleWriter.h
│       ├── JsonWriter.cpp
│       └── JsonWriter.h
└── tests/
    ├── NormalizationTests.cpp
    └── FixtureTests.cpp
```

------------------------------------------------------------------------

## 6. Windows API Strategy by Hardware Type

### 6.1 System UUID

**Goal:** obtain the SMBIOS/system UUID representing the computer
system.

Primary implementation:

-   Query WMI class `Win32_ComputerSystemProduct`.
-   Read its `UUID` property.

Alternative/lower-level implementation:

-   Call `GetSystemFirmwareTable()` with the raw SMBIOS provider.
-   Parse the SMBIOS System Information structure (Type 1).
-   Extract the UUID field.

Recommended first implementation: WMI.

Record:

``` text
component: system
identifierType: SMBIOS System UUID
source: Win32_ComputerSystemProduct.UUID
```

Validation:

-   Reject an all-zero UUID.
-   Reject an all-`FF` UUID.
-   Detect known placeholder strings.
-   Preserve the raw UUID before normalization.

Caveat: virtual machines commonly expose virtual SMBIOS UUIDs, and
firmware/OEM tooling can sometimes change the value.

------------------------------------------------------------------------

### 6.2 Motherboard Serial Number

Primary implementation:

-   WMI class: `Win32_BaseBoard`
-   Properties:
    -   `Manufacturer`
    -   `Product`
    -   `SerialNumber`

Record each baseboard returned rather than assuming exactly one.

Validation should recognize placeholders such as:

``` text
To Be Filled By O.E.M.
Default string
None
Unknown
Not Specified
```

Do not discard these silently; mark them invalid/unreliable and retain
the raw value for diagnostics.

------------------------------------------------------------------------

### 6.3 BIOS / System Firmware Serial

Primary implementation:

-   WMI class: `Win32_BIOS`
-   Read:
    -   `SerialNumber`
    -   `Manufacturer`
    -   `SMBIOSBIOSVersion`

The BIOS serial frequently corresponds to the OEM system/service serial
rather than a serial belonging to the BIOS ROM itself. Label it
according to the Windows field instead of interpreting it as a
cryptographically unique BIOS chip ID.

------------------------------------------------------------------------

## 7. TPM 2.0

TPM requires special treatment because it is fundamentally different
from reading a disk serial.

### Goals

Determine:

1.  Whether a TPM exists.
2.  Whether it is enabled/ready.
3.  TPM specification/version information.
4.  Whether an Endorsement Key identity/certificate can be accessed
    through supported Windows interfaces.
5.  Whether the application can use TPM-backed keys for a stronger
    machine identity.

### Recommended Windows interfaces

Investigate and implement against:

-   Windows TPM Base Services (TBS)
-   CNG / NCrypt platform crypto provider
-   TPM-related WMI/CIM interfaces where appropriate

Relevant Windows components include:

``` text
tbs.h
tbs.lib
ncrypt.h
ncrypt.lib
bcrypt.h
bcrypt.lib
```

### Important architecture decision

Do **not** treat an EK private key as a readable serial number. TPM
private key material is specifically designed not to be exported.

For a robust identity system, prefer:

``` text
Application creates key pair
        ↓
Private key protected by TPM
        ↓
Public key exported
        ↓
Public-key fingerprint becomes application device identity
```

The TPM collector can separately expose TPM metadata and, where
supported and authorized, public endorsement information.

### Optional advanced mode

``` text
hwid.exe --tpm-attestation
```

This can later implement attestation rather than merely inventorying TPM
metadata.

------------------------------------------------------------------------

## 8. NVMe SSD Serial Numbers

Prefer direct storage APIs over WMI.

### Enumeration

Enumerate physical disks using:

``` text
\\.\PhysicalDrive0
\\.\PhysicalDrive1
...
```

Open each device with `CreateFileW()` using the minimum required access.

### Query

Use:

``` text
DeviceIoControl()
IOCTL_STORAGE_QUERY_PROPERTY
```

Query `StorageDeviceProperty` and parse `STORAGE_DEVICE_DESCRIPTOR`.

Useful fields include:

-   Vendor ID
-   Product ID
-   Product revision
-   Serial number

For NVMe-specific information, investigate the documented storage
protocol-specific query path where necessary.

### Output

``` text
Disk 0
Bus: NVMe
Model: ...
Serial: ...
```

Do not assume disk ordering remains stable between boots or hardware
changes.

------------------------------------------------------------------------

## 9. SATA SSD/HDD Serial Numbers

Use the same storage abstraction as NVMe where possible.

Primary query:

``` text
IOCTL_STORAGE_QUERY_PROPERTY
```

Parse:

``` cpp
STORAGE_DEVICE_DESCRIPTOR
```

Inspect bus type to distinguish:

``` text
SATA
ATA
SCSI
USB
NVMe
RAID
```

Where a controller/RAID layer hides the underlying device serial, report
that limitation instead of inventing a fallback.

A storage collector should therefore produce zero or more results:

``` cpp
HardwareResults collectStorageIdentifiers();
```

------------------------------------------------------------------------

## 10. Network Adapter Permanent MAC Addresses

Do not confuse the currently configured MAC address with a permanent
hardware address.

### Enumeration

Use IP Helper API:

``` cpp
GetAdaptersAddresses()
```

This is useful for:

-   adapter enumeration
-   interface LUID/index
-   current physical address
-   friendly name
-   adapter status

### Permanent address

For adapters where Windows/network-driver interfaces expose it, query
the permanent hardware address through the appropriate NDIS/device
interface rather than assuming `GetAdaptersAddresses()` always
represents the factory address.

Collect both fields when available:

``` text
Current MAC
Permanent MAC
```

If they differ:

``` text
potentiallyMutable: true
```

Normalize MAC values as:

``` text
AA:BB:CC:DD:EE:FF
```

Filter carefully rather than blindly excluding:

-   virtual adapters
-   VPN adapters
-   Hyper-V adapters
-   loopback
-   Bluetooth
-   Wi-Fi
-   Ethernet

Expose adapter type in output so the caller can decide what to use.

------------------------------------------------------------------------

## 11. USB Device Serial Numbers

USB serial numbers are optional; many USB devices do not implement them.

### Enumeration

Use Windows device enumeration APIs, preferably Configuration Manager
APIs or SetupAPI.

Possible interfaces:

``` text
CM_Get_Device_Interface_List
SetupDiGetClassDevs
SetupDiEnumDeviceInfo
SetupDiGetDeviceInstanceId
SetupDiGetDeviceRegistryProperty
```

Useful values:

-   VID
-   PID
-   device instance ID
-   serial component of the device identity where supplied by the device
-   friendly name
-   manufacturer

Important distinction:

``` text
USB\VID_1234&PID_5678\SERIAL123
```

may contain a manufacturer-provided serial, while some Windows-created
instance identifiers are not equivalent to a factory serial.

The collector must explicitly label which form was obtained.

------------------------------------------------------------------------

## 12. GPU Identifiers

GPU identity is vendor-dependent.

### Baseline implementation

Enumerate display adapters using:

-   DXGI (`IDXGIFactory`, `IDXGIAdapter`)
-   Windows device enumeration APIs

Collect:

-   vendor ID
-   device ID
-   subsystem ID
-   revision
-   adapter description
-   locally unique Windows adapter identifier where available

These identify the adapter type/instance but are **not necessarily a
globally unique physical GPU serial**.

### Vendor-specific optional providers

Add optional backends later:

``` text
NVIDIA → NVML
AMD    → vendor-supported management/API interfaces
Intel  → vendor-supported interfaces where applicable
```

Only expose a GPU UUID/serial as such when the vendor API explicitly
defines it.

Architecture:

``` cpp
class IGpuIdentityProvider
{
public:
    virtual ~IGpuIdentityProvider() = default;
    virtual HardwareResults collect() = 0;
};
```

Implement:

``` text
DxgiGpuProvider
NvidiaGpuProvider
AmdGpuProvider
IntelGpuProvider
```

The program must continue functioning when vendor libraries are absent.

------------------------------------------------------------------------

## 13. SD Card and eMMC Identity

These devices may expose CID information at the hardware/protocol level,
but generic Windows applications cannot assume direct CID access for
every reader/controller.

Implementation strategy:

1.  Enumerate storage devices.
2.  Determine bus/device type.
3.  Query documented Windows storage properties first.
4.  Attempt protocol-specific queries only when supported by the storage
    stack.
5.  Report CID as unavailable when the controller or driver does not
    expose it.

Never substitute the volume serial number for an SD/eMMC CID.

These are fundamentally different:

``` text
CID                    → hardware/device protocol identity
Volume serial number   → filesystem/format metadata
```

------------------------------------------------------------------------

## 14. CPU Information

Modern x86/x64 processors generally should not be expected to expose a
usable globally unique CPU serial number to ordinary Windows software.

The CPU collector should therefore collect **identification**, not claim
uniqueness.

Use:

``` cpp
__cpuid()
__cpuidex()
```

Collect:

-   CPU vendor
-   family
-   model
-   stepping
-   brand string
-   feature information

Optionally compare this with:

``` text
Win32_Processor.ProcessorId
```

Output should explicitly state:

``` text
Unique CPU serial: unavailable/not supported
```

Do not incorporate CPUID model information into a device identity under
the assumption that it is unique.

------------------------------------------------------------------------

## 15. WMI Infrastructure

Because several collectors need WMI, implement one reusable WMI client.

Initialization flow:

``` text
CoInitializeEx
      ↓
CoInitializeSecurity
      ↓
CoCreateInstance(IWbemLocator)
      ↓
ConnectServer(ROOT\CIMV2)
      ↓
CoSetProxyBlanket
      ↓
ExecQuery(WQL)
      ↓
Enumerate IWbemClassObject
```

Suggested interface:

``` cpp
class WmiClient
{
public:
    explicit WmiClient(std::wstring_view nameSpace);

    std::vector<WmiObject> query(std::wstring_view wql);
};
```

Example:

``` cpp
auto rows = wmi.query(
    L"SELECT UUID, Vendor, Name FROM Win32_ComputerSystemProduct"
);
```

Use RAII for COM initialization and COM interface pointers.

------------------------------------------------------------------------

## 16. Normalization

Create one normalization layer instead of embedding cleanup rules inside
every collector.

Examples:

### UUID

``` text
raw:        4C4C4544-0038-3710-8058-CAC04F...
normalized: 4c4c4544-0038-3710-8058-cac04f...
```

### Disk serial

Trim:

-   leading/trailing whitespace
-   trailing NULs

Do not remove internal spaces unless the relevant device specification
says they are padding.

### MAC

``` text
aabbccddeeff
→
AA:BB:CC:DD:EE:FF
```

### Placeholder detection

Centralize known invalid strings:

``` cpp
bool isPlaceholderHardwareId(std::string_view value);
```

Check case-insensitively for known OEM placeholders and empty/all-zero
values.

------------------------------------------------------------------------

## 17. Output

Default human-readable output:

``` text
SYSTEM
  SMBIOS UUID
    Value:       4C4C4544-....
    Source:      Win32_ComputerSystemProduct.UUID
    Confidence:  FirmwareAssigned
    Mutable:     Possibly

MOTHERBOARD
  Manufacturer: ...
  Product:      ...
  Serial:       ...

TPM
  Present:      Yes
  Version:      2.0
  Ready:        Yes

STORAGE
  PhysicalDrive0
    Bus:         NVMe
    Model:       ...
    Serial:      ...

NETWORK
  Ethernet
    Permanent MAC: ...
    Current MAC:   ...

CPU
  Model: ...
  Processor ID: ...
  Unique serial: Not exposed
```

Also support machine-readable JSON:

``` text
hwid.exe --json
```

Suggested schema:

``` json
{
  "schemaVersion": 1,
  "system": [],
  "motherboard": [],
  "bios": [],
  "tpm": [],
  "storage": [],
  "network": [],
  "usb": [],
  "gpu": [],
  "sdEmmc": [],
  "cpu": []
}
```

------------------------------------------------------------------------

## 18. Error Handling

A hardware inventory utility must expect partial failure.

Do not terminate the complete scan because one collector fails.

Example:

``` text
[OK]   System UUID
[OK]   Motherboard serial
[OK]   BIOS serial
[OK]   TPM detected
[OK]   NVMe disk 0
[WARN] USB device has no serial descriptor
[WARN] GPU vendor UUID unavailable
[WARN] SD CID not exposed by driver
```

Store Windows errors as:

``` text
API name
GetLastError() value
formatted Windows error message
```

For HRESULT-based APIs, retain the HRESULT.

------------------------------------------------------------------------

## 19. Privilege Model

The program should first run without elevation.

Each collector should classify its result as:

``` text
available
not supported
not present
access denied
driver does not expose value
invalid/placeholder
query failed
```

Only document administrator mode for operations that demonstrably
require it.

Do not automatically relaunch elevated.

------------------------------------------------------------------------

## 20. Security and Privacy

Hardware identifiers are persistent identifiers and can become
privacy-sensitive when stored or transmitted.

Recommended rules:

-   Never transmit identifiers automatically.
-   Never write identifiers to disk unless explicitly requested.
-   Do not log TPM certificate material unnecessarily.
-   Provide `--redact` for support/debug output.
-   Avoid using a concatenation of raw serial numbers as a password or
    cryptographic secret.
-   Do not assume hashing an identifier makes a low-entropy or guessable
    identifier secret.
-   Clearly document what `--json` exposes.

Example redaction:

``` text
NVMe Serial: S3********92
MAC:         AA:BB:**:**:EE:FF
UUID:        4C4C****-****-****-****-********1234
```

------------------------------------------------------------------------

## 21. Device Fingerprinting Policy

If the eventual objective is a stable machine identifier, keep that
logic outside the collectors.

Do **not** simply calculate:

``` text
SHA256(
    motherboard_serial +
    disk_serial +
    mac_address
)
```

and assume it permanently identifies the computer.

Hardware changes make that approach brittle.

Instead separate:

``` text
Hardware inventory
        ↓
Identity policy
        ↓
Application device identity
```

For stronger identity:

``` text
First application enrollment
        ↓
Generate asymmetric key
        ↓
Protect private key using TPM/CNG
        ↓
Export public key
        ↓
SHA-256(public key)
        ↓
Application Device ID
```

Hardware serials can then be supporting telemetry rather than the root
identity.

------------------------------------------------------------------------

## 22. Implementation Phases

### Phase 1 --- Project foundation

Implement:

-   CMake project
-   CLI parser
-   common result structure
-   console output
-   JSON output
-   Windows error utilities
-   UTF-16/UTF-8 conversion
-   COM RAII initialization

Deliverable:

``` text
hwid.exe --help
hwid.exe --json
```

### Phase 2 --- SMBIOS/WMI identifiers

Implement:

-   System UUID
-   motherboard serial
-   BIOS serial
-   CPU information

This establishes the reusable WMI infrastructure.

### Phase 3 --- Storage

Implement:

-   physical-drive enumeration
-   `IOCTL_STORAGE_QUERY_PROPERTY`
-   NVMe/SATA/USB storage classification
-   disk model and serial extraction

Test with:

-   NVMe SSD
-   SATA SSD
-   SATA HDD
-   USB-to-SATA enclosure
-   RAID/controller-backed storage if available

### Phase 4 --- Network

Implement:

-   adapter enumeration
-   current MAC
-   permanent MAC where the driver exposes it
-   virtual/physical adapter classification

### Phase 5 --- USB

Implement:

-   device-tree enumeration
-   VID/PID
-   instance information
-   manufacturer serial when available

### Phase 6 --- TPM

Implement:

-   TPM detection
-   TPM version/state
-   supported public identity information
-   optional TPM-backed application key generation

Treat TPM key creation as a separate command rather than silently
changing TPM state during inventory.

For example:

``` text
hwid.exe --tpm-info
hwid.exe --create-device-key
```

### Phase 7 --- GPU

Implement:

1.  DXGI baseline.
2.  Optional NVIDIA provider.
3.  Optional AMD provider.
4.  Optional Intel provider.

Vendor-specific support must be dynamically optional.

### Phase 8 --- SD/eMMC

Implement Windows storage queries and supported protocol-specific paths.

Explicitly handle systems where the controller does not expose CID.

### Phase 9 --- Hardening

Add:

-   unit tests
-   integration tests
-   malformed WMI/property handling
-   missing-device tests
-   access-denied tests
-   virtual-machine tests
-   Unicode device names
-   placeholder detection
-   JSON schema/versioning

------------------------------------------------------------------------

## 23. Testing Matrix

Test on multiple hardware configurations.

  Environment             Key checks
  ----------------------- ---------------------------------------------
  Windows 11 desktop      motherboard, BIOS, TPM, NVMe, Ethernet, GPU
  Windows 11 laptop       SMBIOS, TPM, NVMe, Wi-Fi, integrated GPU
  Windows 10 machine      API compatibility
  Hyper-V VM              virtual SMBIOS/NIC/storage behavior
  VMware VM               virtual identifiers
  Multiple disks          correct per-device association
  USB flash drive         optional USB/storage serial
  USB HDD enclosure       bridge behavior
  SD reader               CID availability
  No TPM / disabled TPM   graceful failure
  Non-admin account       privilege behavior

Tests should verify semantics, not hard-code one developer machine's
serial numbers.

------------------------------------------------------------------------

## 24. Important Edge Cases

Handle all of the following:

-   OEM returns `"To Be Filled By O.E.M."`
-   serial field is empty
-   serial is all zeros
-   UUID is all zeros or all `FF`
-   WMI service unavailable
-   multiple motherboards/baseboards reported
-   multiple GPUs
-   multiple NICs
-   MAC randomization/current MAC differs from permanent MAC
-   virtual NICs
-   USB device has no serial descriptor
-   storage behind RAID controller
-   USB/SATA bridge substitutes its own serial
-   NVMe namespace/controller distinctions
-   TPM disabled in firmware
-   TPM present but unavailable to current process
-   GPU vendor library not installed
-   SD reader hides CID
-   virtual machine presents synthetic identifiers

------------------------------------------------------------------------

## 25. Suggested API Surface

Each collector should have a narrow interface:

``` cpp
namespace hwid
{
    HardwareResults collectSystem();
    HardwareResults collectMotherboard();
    HardwareResults collectBios();
    HardwareResults collectTpm();
    HardwareResults collectStorage();
    HardwareResults collectNetwork();
    HardwareResults collectUsb();
    HardwareResults collectGpu();
    HardwareResults collectSdEmmc();
    HardwareResults collectCpu();
}
```

The application layer then orchestrates them:

``` cpp
HardwareResults collectAll()
{
    HardwareResults result;

    append(result, collectSystem());
    append(result, collectMotherboard());
    append(result, collectBios());
    append(result, collectTpm());
    append(result, collectStorage());
    append(result, collectNetwork());
    append(result, collectUsb());
    append(result, collectGpu());
    append(result, collectSdEmmc());
    append(result, collectCpu());

    return result;
}
```

A failure in one function must not prevent subsequent collectors from
running.

------------------------------------------------------------------------

## 26. Dependencies

Prefer Windows SDK functionality wherever practical.

Likely Windows libraries:

``` text
wbemuuid.lib
ole32.lib
oleaut32.lib
iphlpapi.lib
setupapi.lib
cfgmgr32.lib
dxgi.lib
tbs.lib
ncrypt.lib
bcrypt.lib
```

Exact linkage should be added only when the corresponding implementation
requires it.

For JSON, either:

1.  use a small well-maintained C++ JSON dependency, or
2.  implement a narrowly scoped JSON writer.

Do not hand-build JSON using unescaped string concatenation.

------------------------------------------------------------------------

## 27. Definition of Done

Version 1 is complete when:

-   The program builds cleanly as x64 C++ on supported Windows versions.
-   Standard-user execution works for the normal inventory path.
-   Every requested hardware category has a collector.
-   Unsupported identifiers are explicitly reported rather than
    fabricated.
-   Multiple disks/NICs/GPUs/USB devices are represented independently.
-   Raw values and normalized values remain distinguishable.
-   Placeholder OEM values are detected.
-   Console and JSON output are supported.
-   Errors from one collector do not abort the entire scan.
-   The program never describes CPU model data, a volume serial, a
    transient MAC, or a Windows device instance identifier as a
    guaranteed globally unique hardware serial.
-   TPM-backed identity is kept conceptually separate from passive
    hardware inventory.
-   Tests cover physical machines and at least one virtualized
    environment.

------------------------------------------------------------------------

## 28. Recommended Development Order

The lowest-risk implementation sequence is:

``` text
1. Core result/error/output framework
2. WMI helper
3. System UUID
4. Motherboard
5. BIOS
6. CPU
7. Storage
8. Network
9. USB
10. TPM
11. DXGI GPU enumeration
12. Vendor-specific GPU providers
13. SD/eMMC
14. TPM-backed application identity
15. Integration and VM testing
```

This order gets useful results quickly while leaving the hardware- and
vendor-specific portions until the common Windows infrastructure is
stable.

------------------------------------------------------------------------

## 29. Final Architecture

``` text
                     ┌──────────────────────┐
                     │       hwid.exe       │
                     │ CLI / orchestration  │
                     └──────────┬───────────┘
                                │
                ┌───────────────▼────────────────┐
                │      Collector Interface       │
                └───────────────┬────────────────┘
                                │
       ┌────────────┬───────────┼───────────┬─────────────┐
       │            │           │           │             │
       ▼            ▼           ▼           ▼             ▼
     WMI         Storage      Network      Device        TPM/CNG
  SMBIOS/CPU      IOCTL       IP Helper   Enumeration
       │            │           │           │             │
       └────────────┴───────────┴───────────┴─────────────┘
                                │
                                ▼
                    HardwareIdentifier[]
                                │
                   ┌────────────┴────────────┐
                   ▼                         ▼
              Console output             JSON output
```

The critical boundary is between **inventory** and **identity**. The
inventory layer reports exactly what Windows and the hardware expose; a
separate identity layer can later decide how those observations should
participate in licensing, enrollment, authentication, or diagnostics.
