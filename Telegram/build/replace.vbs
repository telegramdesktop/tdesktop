Dim action, pat, patparts, rxp, inp, matchCount
action = WScript.Arguments(0)
pat = WScript.Arguments(1)
pat = Replace(pat, "&quot;", chr(34))
pat = Replace(pat, "&hat;", "^")
pat = Replace(pat, "&amp;", "&")

Set rxp = new RegExp
rxp.Global = True
rxp.Multiline = False
If action = "Replace" Then
  patparts = Split(pat, "/")
  rxp.Pattern = patparts(0)
Else
  rxp.Pattern = pat
End If

matchCount = 0
Do While Not WScript.StdIn.AtEndOfStream
  inp = WScript.StdIn.ReadLine()
  If rxp.Test(inp) Then
    matchCount = matchCount + 1
  End If
  If action = "Replace" Then
    WScript.Echo rxp.Replace(inp, patparts(1))
  End If
Loop

If action = "Replace" Then
  If matchCount = 0 Then
    WScript.Quit(2)
  End If
Else
  WScript.Echo matchCount
End If
