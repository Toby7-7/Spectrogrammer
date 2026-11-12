$sampleRate = 48000
$duration = 5.0
$freq = 440.0
$numSamples = [int]($sampleRate * $duration)
$samples = New-Object byte[] ($numSamples * 2)

for ($i = 0; $i -lt $numSamples; $i++) {
    $t = $i / $sampleRate
    $val = [short]([Math]::Sin(2 * [Math]::PI * $freq * $t) * 20000)
    $bytes = [BitConverter]::GetBytes($val)
    $samples[$i * 2] = $bytes[0]
    $samples[$i * 2 + 1] = $bytes[1]
}

$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter($ms)
$bw.Write([System.Text.Encoding]::ASCII.GetBytes('RIFF'))
$bw.Write([int](36 + $samples.Length))
$bw.Write([System.Text.Encoding]::ASCII.GetBytes('WAVE'))
$bw.Write([System.Text.Encoding]::ASCII.GetBytes('fmt '))
$bw.Write([int]16)
$bw.Write([short]1) # PCM
$bw.Write([short]1) # Mono
$bw.Write([int]$sampleRate)
$bw.Write([int]($sampleRate * 2))
$bw.Write([short]2)
$bw.Write([short]16)
$bw.Write([System.Text.Encoding]::ASCII.GetBytes('data'))
$bw.Write([int]$samples.Length)
$bw.Write($samples)

[System.IO.File]::WriteAllBytes("$PSScriptRoot\test_440hz.wav", $ms.ToArray())
& "D:\develop\android\platform-tools\adb.exe" -s 192.168.1.4:5555 push "$PSScriptRoot\test_440hz.wav" /sdcard/Android/data/org.nanoorg.spectrogrammer.pr1/files/
