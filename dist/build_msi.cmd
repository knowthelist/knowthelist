"%WIX%\bin\candle.exe" -v -dLang=en -out ..\..\knowthelist.wixobj -arch x86 -ext "%WIX%\bin\WixDifxAppExtension.dll" -ext "%WIX%\bin\WixUtilExtension.dll" -ext "%WIX%\bin\WixUIExtension.dll" knowthelist.wxs
 
"%WIX%\bin\Light.exe" -v -cultures:en-us -ext "%WIX%\bin\WixDifxAppExtension.dll" -ext "%WIX%\bin\WixUtilExtension.dll" -ext "%WIX%\bin\WixUIExtension.dll" -out ..\..\Knowthelist-Setup.msi -pdbout ..\..\knowthelist.wixpdb -sice:ICE09 ..\..\knowthelist.wixobj "%WIX%\bin\difxapp_x86.wixlib"
 