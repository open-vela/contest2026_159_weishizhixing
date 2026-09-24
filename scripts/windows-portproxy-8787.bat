@echo off
REM Run on Windows HOST while phone hotspot "mfl" is on, and this PC is connected to mfl.
REM Forwards WiFi:8787 -> VMware NAT guest cloud (default 192.168.80.128:8787)

set GUEST=192.168.80.128
set PORT=8787

echo === Your WiFi IPv4 on mfl (board should use this as CLOUD_HOST) ===
ipconfig | findstr /i "IPv4"
echo.
echo Enabling portproxy %PORT% -^> %GUEST%:%PORT% (need Admin)...
netsh interface portproxy delete v4tov4 listenport=%PORT% listenaddress=0.0.0.0 >nul 2>&1
netsh interface portproxy add v4tov4 listenport=%PORT% listenaddress=0.0.0.0 connectport=%PORT% connectaddress=%GUEST%
netsh advfirewall firewall delete rule name="jianwei-cloud-8787" >nul 2>&1
netsh advfirewall firewall add rule name="jianwei-cloud-8787" dir=in action=allow protocol=TCP localport=%PORT%
netsh interface portproxy show all
echo.
echo Done. Keep VM cloud running. Board CLOUD_HOST = this PC's mfl IPv4 above.
pause
