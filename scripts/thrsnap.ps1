# Per-thread CPU of MyGameRecomp between two moments: thrsnap.ps1 -Wait1 <s> -Wait2 <s> -Out <file>
# (start address in hex identifies the thread entry; compare with the .map)
param([int]$Wait1 = 360, [int]$Wait2 = 480, [string]$Out = "D:\GOW A\work\thrsnap.txt")
Start-Sleep -Seconds $Wait1
$p = Get-Process MyGameRecomp -ErrorAction Stop
$a = @{}; foreach ($t in $p.Threads) { try { $a[$t.Id] = $t.TotalProcessorTime.TotalSeconds } catch {} }
$t0 = Get-Date
Start-Sleep -Seconds ($Wait2 - $Wait1)
$p = Get-Process MyGameRecomp -ErrorAction Stop
$el = ((Get-Date) - $t0).TotalSeconds
$base = [int64]$p.MainModule.BaseAddress
$rows = foreach ($t in $p.Threads) {
    try {
        $d = $t.TotalProcessorTime.TotalSeconds - $(if ($a.ContainsKey($t.Id)) { $a[$t.Id] } else { 0 })
        $sa = [int64]$t.StartAddress
        [pscustomobject]@{ Tid = $t.Id; Cores = [math]::Round($d / $el, 3); Start = ('{0:X}' -f $sa); Rva = ('{0:X}' -f ($sa - $base)); Prio = $t.PriorityLevel }
    } catch {}
}
"elapsed $([math]::Round($el,1)) s, base $('{0:X}' -f $base)" | Out-File -Encoding utf8 $Out
$rows | Sort-Object Cores -Descending | Format-Table -AutoSize | Out-String -Width 200 | Out-File -Encoding utf8 -Append $Out
