# Makes "THEMOD 1.3.exe" from your own THUG2.exe (in the same folder), the way THEMOD 1.3's installer does:
# the same 126 bytes. THUG2.exe itself is left as it is; an existing "THEMOD 1.3.exe" is never overwritten.
$ErrorActionPreference = 'Stop'
$dir = Split-Path -Parent $MyInvocation.MyCommand.Path
$src = Join-Path $dir 'THUG2.exe'
$dst = Join-Path $dir 'THEMOD 1.3.exe'

$size = 2695168
$srcSha = '52FC88849654B34839EC2F96BFF3A8C0B7A855DF9A207AAB9F2FCA2E6BD440F3' # THUG2.exe, PC retail
$dstSha = '25874001BE0C1F12DF47BC8F0DAD61869CAE0FE90964C12245165732A4CBB5EA' # THEMOD 1.3.exe from its installer

# file offset, THUG2.exe bytes, THEMOD 1.3 bytes
$patches = @(
    @(0x1A8A01, '759C', '9090'), # the save's checksum isn't checked
    @(0x1F95BB, '75', 'EB'),     # GameSpy CD key check skipped
    @(0x24CD99, '7567326F6E6C696E652E636F6D2F6D6F74642E64617400687474703A2F2F7777772E74687567326F6E6C696E652E636F6D2F746573746D6F74642E646174', '6D6F64732E636F6D2F6D6F74642E646174000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'),
    @(0x254491, '65', '61'),
    @(0x257698, '71625F736372697074732E7072650000416C6C20737461727475702051422066696C657300', '74686574687567326D6F642E7072650000416C6C20737461727475702051422066696C6573'), # thethug2mod.pre
    @(0x267D60, '2E67616D65737079', '312E74686D6F6473'),
    @(0x268074, '47616D65537079485454502F312E30', '5448454D4F44313300000000000000')
)

function Hex([string]$s) {
    $b = New-Object byte[] ($s.Length / 2)
    for ($i = 0; $i -lt $b.Length; $i++) { $b[$i] = [Convert]::ToByte($s.Substring($i * 2, 2), 16) }
    return ,$b
}

function Sha([byte[]]$b) {
    $sha = [Security.Cryptography.SHA256]::Create()
    return ([BitConverter]::ToString($sha.ComputeHash($b))).Replace('-', '')
}

function Done([string]$text, [int]$code) {
    Write-Host $text
    exit $code
}

if (Test-Path -LiteralPath $dst) {
    Done '"THEMOD 1.3.exe" is already here, nothing to do. / "THEMOD 1.3.exe" uzhe est, nichego ne delayu.' 0
}
if (-not (Test-Path -LiteralPath $src)) {
    Done 'THUG2.exe not found: put this patcher into the game folder (Game). / THUG2.exe ne nayden: polozhite patcher v papku igry (Game).' 1
}

$bytes = [IO.File]::ReadAllBytes($src)
if ($bytes.Length -ne $size) {
    Done "THUG2.exe is $($bytes.Length) bytes, expected ${size}: an unknown version. / Neizvestnaya versiya THUG2.exe." 1
}
$known = (Sha $bytes) -eq $srcSha

foreach ($p in $patches) {
    $old = Hex $p[1]
    $new = Hex $p[2]
    for ($i = 0; $i -lt $old.Length; $i++) {
        if ($bytes[$p[0] + $i] -ne $old[$i]) {
            Done ('THUG2.exe differs at 0x{0:X}: an unknown or already patched version. / THUG2.exe ne podhodit (izmenyon ili drugaya versiya).' -f ($p[0] + $i)) 1
        }
    }
    [Array]::Copy($new, 0, $bytes, $p[0], $new.Length)
}

[IO.File]::WriteAllBytes($dst, $bytes)
if ($known -and (Sha $bytes) -ne $dstSha) {
    Remove-Item -LiteralPath $dst
    Done 'Internal error: the result does not match THEMOD 1.3. / Oshibka: rezultat ne sovpal s THEMOD 1.3.' 1
}
if (-not $known) {
    Write-Host 'Note: THUG2.exe is not the known retail version, but all patched bytes matched.'
}
Done '"THEMOD 1.3.exe" created. / "THEMOD 1.3.exe" sozdan.' 0
