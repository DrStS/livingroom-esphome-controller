# Read-only Inventar: Wer im LAN kann ein SMA-Datenlogger sein?
# Sucht nach WebBox / Cluster Controller / Speedwire-Wechselrichtern / Modbus TCP.
# Keine Schreibzugriffe, nur Ping, TCP-Connect-Test, HTTP GET, Speedwire-Discovery.

$ErrorActionPreference = 'Continue'
$out = Join-Path $PSScriptRoot 'sma_barn_probe.txt'
$lines = @()
$lines += "Lauf: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
$lines += ""

# ---------- 1) Ping-Sweep ----------
$lines += "=== ERREICHBARE HOSTS ==="
$pings = @()
foreach ($i in 1..254) {
    $p = New-Object System.Net.NetworkInformation.Ping
    $pings += [pscustomobject]@{ IP = "192.168.1.$i"; Task = $p.SendPingAsync("192.168.1.$i", 1200) }
}
$alive = @()
foreach ($e in $pings) {
    try {
        $r = $e.Task.GetAwaiter().GetResult()
        if ($r.Status -eq 'Success') { $alive += $e.IP }
    } catch {}
}
$lines += ($alive -join ', ')
$lines += "Anzahl: $($alive.Count)"
$lines += ""

# ---------- 2) ARP / MAC-Hersteller ----------
$lines += "=== ARP (MAC je Host) ==="
$arp = (arp -a) -split "`r?`n"
foreach ($ip in $alive) {
    $row = $arp | Where-Object { $_ -match ("\s" + [regex]::Escape($ip) + "\s") } | Select-Object -First 1
    if ($row) { $lines += ($row.Trim()) }
}
$lines += ""

# ---------- 3) TCP-Ports ----------
function Test-Port($ip, $port, $ms = 500) {
    $c = New-Object System.Net.Sockets.TcpClient
    try {
        $iar = $c.BeginConnect($ip, $port, $null, $null)
        if ($iar.AsyncWaitHandle.WaitOne($ms, $false) -and $c.Connected) { $c.Close(); return $true }
    } catch {}
    finally { try { $c.Close() } catch {} }
    return $false
}

$ports = @(21, 80, 443, 502, 1502, 8080, 9522)
$lines += "=== OFFENE TCP-PORTS ==="
foreach ($ip in $alive) {
    $open = @()
    foreach ($pt in $ports) { if (Test-Port $ip $pt) { $open += $pt } }
    if ($open.Count -gt 0) { $lines += "$ip -> $($open -join ', ')" }
    else { $lines += "$ip -> (keine der geprueften)" }
}
$lines += ""

# ---------- 4) HTTP-Fingerprint ----------
$lines += "=== HTTP-FINGERPRINT (Port 80 / 443) ==="
foreach ($ip in $alive) {
    foreach ($scheme in @('http', 'https')) {
        $port = if ($scheme -eq 'http') { 80 } else { 443 }
        if (-not (Test-Port $ip $port)) { continue }
        try {
            $resp = Invoke-WebRequest -Uri "$scheme`://$ip/" -TimeoutSec 5 -UseBasicParsing `
                -MaximumRedirection 2 -ErrorAction Stop
            $srv = $resp.Headers['Server']
            $ttl = ''
            if ($resp.Content -match '(?is)<title>(.{0,120}?)</title>') { $ttl = $Matches[1].Trim() }
            $lines += "$ip $scheme`: HTTP $($resp.StatusCode) Server='$srv' Title='$ttl' Len=$($resp.RawContentLength)"
        } catch {
            $lines += "$ip $scheme`: $($_.Exception.Message -replace "`r?`n", ' ')"
        }
    }
}
$lines += ""

# ---------- 5) SMA-typische Pfade ----------
$lines += "=== SMA-PFADE (WebBox /rpc, Webconnect /dyn/login.json, ClusterCtrl /culture) ==="
$paths = @('/rpc', '/dyn/login.json', '/culture', '/home.htm', '/index.html')
foreach ($ip in $alive) {
    if (-not (Test-Port $ip 80)) { continue }
    foreach ($pa in $paths) {
        try {
            $r = Invoke-WebRequest -Uri "http://$ip$pa" -TimeoutSec 4 -UseBasicParsing -ErrorAction Stop
            $lines += "$ip $pa -> HTTP $($r.StatusCode) Len=$($r.RawContentLength)"
        } catch {
            $sc = ''
            try { $sc = [int]$_.Exception.Response.StatusCode } catch {}
            $lines += "$ip $pa -> Fehler/$sc"
        }
    }
}
$lines += ""

# ---------- 6) Speedwire-Discovery unicast an jeden Host ----------
$lines += "=== SPEEDWIRE-DISCOVERY (unicast UDP 9522) ==="
$disc = [byte[]](0x53, 0x4D, 0x41, 0x00, 0x00, 0x04, 0x02, 0xA0, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00)
$udp = New-Object System.Net.Sockets.UdpClient
$udp.Client.ReceiveTimeout = 1500
foreach ($ip in $alive) {
    try { $null = $udp.Send($disc, $disc.Length, $ip, 9522) } catch {}
}
$deadline = (Get-Date).AddSeconds(8)
$seen = @{}
while ((Get-Date) -lt $deadline) {
    try {
        $ep = New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Any, 0)
        $data = $udp.Receive([ref]$ep)
    } catch { continue }
    $src = $ep.Address.ToString()
    $hex = ($data[0..([Math]::Min(59, $data.Length - 1))] | ForEach-Object { $_.ToString('X2') }) -join ''
    $key = "$src len=$($data.Length) hex=$hex"
    if (-not $seen.ContainsKey($key)) { $seen[$key] = 1; $lines += $key } else { $seen[$key]++ }
}
try { $udp.Close() } catch {}
if ($seen.Count -eq 0) { $lines += "keine Antwort" }

$lines | Out-File -FilePath $out -Encoding utf8
