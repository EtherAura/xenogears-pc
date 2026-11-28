# Run one headless state replay in the Windows guest's case directory.
$ErrorActionPreference = 'Stop'
$case = Get-Content -LiteralPath case.json -Raw | ConvertFrom-Json
foreach ($value in $case.env.PSObject.Properties) {
    [Environment]::SetEnvironmentVariable($value.Name, $value.Value)
}
if (!(Test-Path -LiteralPath $case.disc) -or !(Test-Path -LiteralPath $case.env.XG_DISC2)) {
    throw 'The Windows runner needs both retail discs'
}
$start = [Diagnostics.ProcessStartInfo]::new()
$start.FileName = $case.exe
$start.Arguments = '"' + $case.disc + '"'
$start.WorkingDirectory = (Get-Location).Path
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$process = [Diagnostics.Process]::Start($start)
$stdout = $process.StandardOutput.ReadToEndAsync()
$stderr = $process.StandardError.ReadToEndAsync()
if ($process.WaitForExit(600000)) {
    $code = $process.ExitCode
} else {
    $process.Kill()
    $process.WaitForExit()
    $code = 124
}
[IO.File]::WriteAllText((Join-Path $start.WorkingDirectory 'port.log'), $stdout.Result + $stderr.Result)
$files = @('states', 'cards', 'port.log', 'invariants.log', 'sweep.log') | Where-Object { Test-Path $_ }
tar.exe -cf outputs.tar @files
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
exit $code
