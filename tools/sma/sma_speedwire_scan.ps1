# SMA-Speedwire-Inventar (nur lesend), Variante 3.
# Laengerer Lauf, mehrfach Discovery, protokolliert ALLE Antwortenden.

$ErrorActionPreference = 'Continue'
$local = [System.Net.IPAddress]::Parse('192.168.1.97')
$group = [System.Net.IPAddress]::Parse('239.12.255.254')
$port = 9522
$outFile = Join-Path $PSScriptRoot 'sma_inventory.txt'
$lines = @()

$udp = New-Object System.Net.Sockets.UdpClient
$udp.Client.SetSocketOption([System.Net.Sockets.SocketOptionLevel]::Socket,
                            [System.Net.Sockets.SocketOptionName]::ReuseAddress, $true)
$udp.Client.Bind((New-Object System.Net.IPEndPoint($local, $port)))
$udp.JoinMulticastGroup($group, $local)
$udp.Client.ReceiveTimeout = 2000

$disc = [byte[]](0x53,0x4D,0x41,0x00,0x00,0x04,0x02,0xA0,0xFF,0xFF,0xFF,0xFF,
                 0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x00)
$gep = New-Object System.Net.IPEndPoint($group, $port)

$em = @{}       # Energy-Meter-Telegramme (0x6069)
$other = @{}    # alles andere (Discovery-Antworten, Wechselrichter)
$count = 0
$nextDisc = Get-Date
$deadline = (Get-Date).AddSeconds(60)

while ((Get-Date) -lt $deadline) {
    if ((Get-Date) -ge $nextDisc) {
        try { $null = $udp.Send($disc, $disc.Length, $gep) } catch {}
        $nextDisc = (Get-Date).AddSeconds(10)
    }
    try {
        $ep = New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Any, 0)
        $data = $udp.Receive([ref]$ep)
    } catch { continue }

    $count++
    $src = $ep.Address.ToString()
    if ($src -eq '192.168.1.97') { continue }   # eigene Discovery ignorieren

    $proto = if ($data.Length -ge 18) { [uint16]($data[16] * 256 + $data[17]) } else { 0 }

    if ($proto -eq 0x6069 -and $data.Length -ge 24) {
        $susy = [uint16]($data[18] * 256 + $data[19])
        $serial = [uint32]$data[20] * 16777216 + [uint32]$data[21] * 65536 +
                  [uint32]$data[22] * 256 + [uint32]$data[23]
        $key = "src=$src SUSyID=$susy SERIAL=$serial len=$($data.Length)"
        if (-not $em.ContainsKey($key)) { $em[$key] = 1 } else { $em[$key]++ }
    } else {
        $hex = ($data[0..([Math]::Min(47, $data.Length - 1))] |
                ForEach-Object { $_.ToString('X2') }) -join ''
        $key = "src=$src proto=0x$($proto.ToString('X4')) len=$($data.Length) hex48=$hex"
        if (-not $other.ContainsKey($key)) { $other[$key] = 1 } else { $other[$key]++ }
    }
}

$lines += "=== ENERGY-METER-TELEGRAMME (Protokoll 0x6069) ==="
if ($em.Count -eq 0) { $lines += "keine" }
foreach ($k in $em.Keys) { $lines += "$k -> $($em[$k])x" }
$lines += ""
$lines += "=== SONSTIGE SPEEDWIRE-ANTWORTEN (Discovery / Wechselrichter) ==="
if ($other.Count -eq 0) { $lines += "keine" }
foreach ($k in $other.Keys) { $lines += "$k -> $($other[$k])x" }
$lines += ""
$lines += "Pakete gesamt: $count"

try { $udp.DropMulticastGroup($group) } catch {}
$udp.Close()
$lines | Out-File -FilePath $outFile -Encoding utf8
