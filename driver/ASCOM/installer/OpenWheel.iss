; Inno Setup script for the OpenWheel ASCOM driver.
; Build:  ISCC.exe OpenWheel.iss  [/DAppVersion=0.1.0]   (after: dotnet build ..\OpenWheel.Ascom -c Release)
; The installer needs administrator rights (it registers a COM server for all users).

#define AppName "OpenWheel ASCOM Driver"
#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#define ServerExe "OpenWheel.Ascom.Server.exe"
#define BuildDir "..\OpenWheel.Ascom\bin\Release\net48"

[Setup]
; Fixed GUID: identifies the application for upgrades and uninstall. Do not change.
AppId={{B6E2C1D4-5A7F-4C3B-9E21-7D0A4F8E6C13}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=MartinsOpenAstroTools
AppPublisherURL=https://github.com/xlazam01/OpenWheel
AppSupportURL=https://github.com/xlazam01/OpenWheel/issues
; The server is a 32-bit program (like most ASCOM local servers); it serves 32 and 64 bit clients.
DefaultDirName={commonpf32}\OpenWheel
DefaultGroupName=OpenWheel
PrivilegesRequired=admin
MinVersion=10.0
OutputDir=Output
OutputBaseFilename=OpenWheel-ASCOM-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
LicenseFile=..\..\..\LICENSE
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\{#ServerExe}
; a running driver (a client such as NINA is connected) is closed or a restart is offered
CloseApplications=yes

[Files]
Source: "{#BuildDir}\{#ServerExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\{#ServerExe}.config"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\OpenWheel.Core.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
; Starting the server without arguments opens the setup dialog (choose wheel, filter names, status)
Name: "{group}\OpenWheel setup"; Filename: "{app}\{#ServerExe}"
Name: "{group}\Uninstall OpenWheel"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\{#ServerExe}"; Parameters: "/regserver /silent"; StatusMsg: "Registering the ASCOM driver..."; Flags: runhidden waituntilterminated

[UninstallRun]
Filename: "{app}\{#ServerExe}"; Parameters: "/unregserver /silent"; RunOnceId: "UnregisterOpenWheel"; Flags: runhidden waituntilterminated

[Code]
// The driver implements IFilterWheelV3, which needs ASCOM Platform 7 or newer.
function InitializeSetup(): Boolean;
var
  V: String;
  Major: Integer;
begin
  Result := True;
  if not RegQueryStringValue(HKLM32, 'SOFTWARE\ASCOM\Platform', 'Platform Version', V) then
    V := '';
  if V = '' then V := 'none found';
  Major := StrToIntDef(Copy(V, 1, Pos('.', V) - 1), 0);
  if Major < 7 then
  begin
    MsgBox('The OpenWheel driver needs ASCOM Platform 7 or newer.' + #13#10 +
           'Installed: ' + V + #13#10#13#10 +
           'Please install it from https://ascom-standards.org and run this setup again.',
           mbError, MB_OK);
    Result := False;
  end;
end;
