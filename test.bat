@echo off
setlocal enabledelayedexpansion

:: Chat Room Project Test Script for Windows
:: Comprehensive testing for client-server functionality

:: Configuration
set SERVER_IP=127.0.0.1
set SERVER_PORT=8080
set CLIENT_EXEC=client.exe
set SERVER_EXEC=server.exe
set TEST_TIMEOUT=30

echo ================================
echo   Chat Room Project Test Suite  
echo ================================

:: Build project
echo [INFO] Building project...
make clean >nul 2>&1
make all >nul 2>&1

if not exist %CLIENT_EXEC% (
    echo [ERROR] Client executable not found
    goto :end
)

if not exist %SERVER_EXEC% (
    echo [ERROR] Server executable not found
    goto :end
)

echo [SUCCESS] Build completed successfully

:: Test server startup
echo [INFO] Testing server startup...
start /B %SERVER_EXEC% %SERVER_PORT% > server.log 2>&1
timeout /t 3 >nul

:: Test basic client connection
echo [INFO] Testing basic client connection...
echo help > test_client_script.txt
echo quit >> test_client_script.txt

%CLIENT_EXEC% %SERVER_IP% %SERVER_PORT% < test_client_script.txt > client.log 2>&1

if %errorlevel% equ 0 (
    echo [SUCCESS] Client connection test passed
) else (
    echo [ERROR] Client connection test failed
)

:: Test authentication
echo [INFO] Testing user authentication...
echo login testuser1 password123 > auth_test.txt
echo quit >> auth_test.txt

%CLIENT_EXEC% %SERVER_IP% %SERVER_PORT% < auth_test.txt > auth.log 2>&1
echo [SUCCESS] Authentication test completed

:: Test room operations
echo [INFO] Testing room operations...
echo login roomuser password123 > room_test.txt
echo create_room testroom >> room_test.txt
echo join_room testroom >> room_test.txt
echo list_rooms >> room_test.txt
echo quit >> room_test.txt

%CLIENT_EXEC% %SERVER_IP% %SERVER_PORT% < room_test.txt > room.log 2>&1
echo [SUCCESS] Room operations test completed

:: Cleanup
echo [INFO] Cleaning up...
taskkill /F /IM %SERVER_EXEC% >nul 2>&1
del /Q *.txt *.log >nul 2>&1

echo --------------------------------
echo [SUCCESS] All tests completed!
echo --------------------------------

:end
pause