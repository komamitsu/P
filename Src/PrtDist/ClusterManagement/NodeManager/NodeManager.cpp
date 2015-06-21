#include "NodeManager.h"

/* GLobal Variables */
char* logFileName = "PRTDIST_NODEMANAGER.txt";
int CurrentNodeID = 1;
int myNodeId;
FILE* logFile;
std::mutex g_lock;
//
// Helper Functions
//

void PrtDistNodeManagerCreateLogFile()
{
	g_lock.lock();
	fopen_s(&logFile, logFileName, "w+");
	fputs("Starting NodeManager Service ..... \n", logFile);
	fflush(logFile);
	fclose(logFile);
	g_lock.unlock();
}


void PrtDistNodeManagerLog(char* log)
{
	g_lock.lock();
	fopen_s(&logFile, logFileName, "a+");
	fputs(log, logFile);
	fputs("\n", logFile);
	fflush(logFile);
	fclose(logFile);
	g_lock.unlock();
}

int PrtDistNodeManagerNextContainerId()
{
	static int counter = 0;
	g_lock.lock();
	counter = counter + 1;
	g_lock.unlock();
	return (counter);
}

void PrtDistNodeManagerCreateRPCServer()
{
	PrtDistNodeManagerLog("Creating RPC server for NodeManager ....");
	RPC_STATUS status;
	char buffPort[100];
	_itoa_s(PRTD_SERVICE_PORT, buffPort, 10);
	status = RpcServerUseProtseqEp(
		reinterpret_cast<unsigned char*>("ncacn_ip_tcp"), // Use TCP/IP protocol.
		RPC_C_PROTSEQ_MAX_REQS_DEFAULT, // Backlog queue length for TCP/IP.
		(RPC_CSTR)buffPort, // TCP/IP port to use.
		NULL);

	if (status)
	{
		PrtDistNodeManagerLog("Runtime reported exception in RpcServerUseProtseqEp");
		exit(status);
	}

	status = RpcServerRegisterIf2(
		s_PrtDistNodeManager_v1_0_s_ifspec, // Interface to register.
		NULL, // Use the MIDL generated entry-point vector.
		NULL, // Use the MIDL generated entry-point vector.
		RPC_IF_ALLOW_CALLBACKS_WITH_NO_AUTH, // Forces use of security callback.
		RPC_C_LISTEN_MAX_CALLS_DEFAULT, // Use default number of concurrent calls.
		(unsigned)-1, // Infinite max size of incoming data blocks.
		NULL); // Naive security callback.

	if (status)
	{
		PrtDistNodeManagerLog("Runtime reported exception in RpcServerRegisterIf2");
		exit(status);
	}

	PrtDistNodeManagerLog("Node Manager is listening ...");
	// Start to listen for remote procedure calls for all registered interfaces.
	// This call will not return until RpcMgmtStopServerListening is called.
	status = RpcServerListen(
		1, // Recommended minimum number of threads.
		RPC_C_LISTEN_MAX_CALLS_DEFAULT, // Recommended maximum number of threads.
		0);

	if (status)
	{
		PrtDistNodeManagerLog("Runtime reported exception in RpcServerListen");
		exit(status);
	}

}


//
// RPC Service
//

// Ping service
void s_PrtDistNMPing(handle_t mHandle, int server,  boolean* amAlive)
{
	char log[100] = "";
	_CONCAT(log, "Pinged by External Server :", PRTD_CLUSTERMACHINES[server]);
	*amAlive = !(*amAlive);
	PrtDistNodeManagerLog(log);
}

// Create NodeManager
void s_PrtDistNMCreateContainer(handle_t mHandle, boolean createMain, int* containerId, boolean *status)
{
	string networkShare = PrtDistClusterConfigGet(NetworkShare);
	string jobFolder = networkShare;
	string localJobFolder = PrtDistClusterConfigGet(localFolder);
	string newLocalJobFolder = localJobFolder;
	boolean st = _ROBOCOPY(jobFolder, newLocalJobFolder);
	if (!st)
	{
		*status = st;
		PrtDistNodeManagerLog("CreateProcess for Node Manager failed in ROBOCOPY\n");
		return;
	}

	//get the exe name
	string exeName = PrtDistClusterConfigGet(MainExe);
	*containerId = PrtDistNodeManagerNextContainerId();
	char commandLine[100];
	sprintf_s(commandLine, 100, "%s %d %d %d", exeName.c_str(), createMain, *containerId, myNodeId);
	//create the node manager process
	STARTUPINFO si;
	PROCESS_INFORMATION pi;

	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	ZeroMemory(&pi, sizeof(pi));

	char currDir[100];
	GetCurrentDirectory(100, currDir);
	SetCurrentDirectory(newLocalJobFolder.c_str());
	
	// Start the child process. 
	if (!CreateProcess(NULL,   // No module name (use command line)
		const_cast<LPSTR>(commandLine),        // Command line
		NULL,           // Process handle not inheritable
		NULL,           // Thread handle not inheritable
		FALSE,          // Set handle inheritance to FALSE
		0,              // No creation flags
		NULL,           // Use parent's environment block
		NULL,           // Use parent's starting directory 
		&si,            // Pointer to STARTUPINFO structure
		&pi)           // Pointer to PROCESS_INFORMATION structure
		)
	{
		PrtDistNodeManagerLog("CreateProcess for Node Manager failed\n");
		*status = false;
		return;
	}

	*status = true;
}



//Helper Functions
int PrtDistCentralServerGetNextID()
{
	g_lock.lock();
	int retValue = CurrentNodeID;
	int totalNodes = atoi(PrtDistClusterConfigGet(TotalNodes));
	if (totalNodes == 0)
	{
		//local node execution
		retValue = 0;
	}
	else
	{
		if (CurrentNodeID == totalNodes)
			CurrentNodeID = 1;
		else
			CurrentNodeID = CurrentNodeID + 1;
	}
	g_lock.unlock();
	return retValue;
}

void s_PrtDistCentralServerGetNodeId(handle_t handle, int server, int *nodeId)
{
	char log[100] = "";
	*nodeId = PrtDistCentralServerGetNextID();
	_CONCAT(log, "Received Request for a new NodeId from ", PRTD_CLUSTERMACHINES[server]);
	_CONCAT(log, " and returned node ID : ", PRTD_CLUSTERMACHINES[*nodeId]);
	PrtDistNodeManagerLog(log);
}

void* __RPC_API
MIDL_user_allocate(size_t size)
{
	unsigned char* ptr;
	ptr = (unsigned char*)malloc(size);
	return (void*)ptr;
}

void __RPC_API
MIDL_user_free(void* object)

{
	free(object);
}


///
///PrtDist Deployer Logging
///


///
/// Main
///
int main(int argc, char* argv[])
{

	PrtDistNodeManagerCreateLogFile();
	if (argc != 2)
	{
		PrtDistNodeManagerLog("ERROR : Wrong number of commandline arguments passed\n");
		exit(-1);
	}
	else
	{

		myNodeId = atoi(argv[1]);
		int totalNodes = atoi(PrtDistClusterConfigGet(TotalNodes));
		if (myNodeId >= totalNodes)
		{
			PrtDistNodeManagerLog("ERROR : Wrong nodeId passed as commandline argument\n");
		}

		char log[100];
		sprintf_s(log, 100, "Started NodeManager at : %d", myNodeId);
		PrtDistNodeManagerLog(log);
		

	}

	PrtDistNodeManagerCreateRPCServer();
	
}