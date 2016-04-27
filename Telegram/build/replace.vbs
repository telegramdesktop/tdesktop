Dim pat, patparts, rxp, inp, found
pat = WScript.Arguments(0)
pat = Replace(pat, "&quot;", chr(34))
pat = Replace(pat, "&hat;", "^")
pat = Replace(pat, "&amp;", "&")
patparts = Split(pat,"/")
Set rxp = new RegExp
found = False
rxp.Global = True
rxp.Multiline = False
rxp.Pattern = patparts(0)
Do While Not WScript.StdIn.AtEndOfStream
  inp = WScript.StdIn.ReadLine()
  If not found Then
    If rxp.Test(inp) Then
      found = True
    End If
  End If
  WScript.Echo rxp.Replace(inp, patparts(1))
Loop
If not found Then
  WScript.Quit(2)
End If
