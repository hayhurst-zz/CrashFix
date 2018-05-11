//! \file main.cpp
//! \brief Entry point to daemon console application.
//! \author Oleg Krivtsov
//! \date Feb 2012

#include "stdafx.h"
#include "DaemonConsole.h"
#include "strconv.h"

// Launch flow (windows):
// Service Control Manager 
//   -> crashfixxd.exe [as monitor]
//        -> crashfixxd.exe --run [as daemon]
//             -> crashfixxd.exe --start -c "C:\Program Files (x86)\CrashFix\bin\..\conf\crashfixd.conf" --run-as-monitor 16616 [start service and exit]

/*
*  Main function.
*/

int main(int argc, char* argv[])
{
#ifdef _WIN32
	ShowWindow(GetConsoleWindow(), SW_HIDE);
#endif

	CDaemonConsole DaemonConsole;
	
	int nResult = DaemonConsole.Run(argc, argv);

	if(!DaemonConsole.GetErrorMsg().empty())
    {
        printf("%s\n", DaemonConsole.GetErrorMsg().c_str());
        printf("\nType --help for available commands.\n\n");
        return 1;
    }

	return nResult;
}

