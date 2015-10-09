#include "RemoteControllerFrameWork.h"

RemoteControllerFrameWork G_RemoteFrameWork;

RemoteControllerFrameWork::RemoteControllerFrameWork(void)
{
	m_ExecFsm=REMOTECONTROLLER_EXEC_FSM_NONE;
	m_FSM=REMOTECONTROLLER_FSM_DISCONNECT;
	m_CurrentOperateClient=-1;
}


RemoteControllerFrameWork::~RemoteControllerFrameWork(void)
{
}

BOOL RemoteControllerFrameWork::Initialize()
{
	m_Lexer.RegisterSpacer(' ');
	m_Lexer.RegisterSpacer('\r');
	m_Lexer.RegisterSpacer('\n');


	m_Grammer.SetLexer(&m_Lexer);
	
	CubeBlockType BtList=CubeBlockType(REMOTESHELL_CCMD_LIST,2,GRAMMAR_TOKEN,GRAMMAR_TOKEN_CMDLIST);
	CubeBlockType BtConnect=CubeBlockType(REMOTESHELL_CCMD_CONNECT,2,GRAMMAR_TOKEN,GRAMMAR_TOKEN_CONNECT);
	CubeBlockType BtLogin=CubeBlockType(REMOTESHELL_CCMD_LOGIN,2,GRAMMAR_TOKEN,GRAMMAR_TOKEN_LOGIN);
	CubeBlockType Param=CubeBlockType(NULL,1,GRAMMAR_PARAMETER);
	CubeBlockType Spacer=CubeBlockType(NULL,1,GRAMMAR_SPACER);
	CubeBlockType Number=CubeBlockType("[0-9]+",2,GRAMMAR_TOKEN,GRAMMAR_TOKEN_NUMBER);
	Number.AsRegex();

	m_Grammer.RegisterBlockType(BtConnect);
	m_Grammer.RegisterBlockType(BtList);
	m_Grammer.RegisterBlockType(BtLogin);
	m_Grammer.RegisterBlockType(Number);
	m_Grammer.RegisterBlockType(Param);

	m_Grammer.RegisterDiscard(Spacer);

	CubeGrammarSentence Sen;
	Sen.add(BtList);

	m_list=m_Grammer.RegisterSentence(Sen);

	Sen.Reset();
	Sen.add(BtConnect);
	Sen.add(Number);

	m_connect=m_Grammer.RegisterSentence(Sen);

	Sen.Reset();
	Sen.add(BtLogin);
	m_Login=m_Grammer.RegisterSentence(Sen);


	m_ServerAddrin.sin_family=AF_INET;

	if (INADDR_NONE == inet_addr(REMOTESHELL_SERVER_IPADDR))
			return FALSE;
		
	m_ServerAddrin.sin_port=htons(REMOTESHELL_SERVER_PORT);
	m_ServerAddrin.sin_addr.s_addr=inet_addr(REMOTESHELL_SERVER_IPADDR);


	Cube_SocketUDP_IO __IO;
	__IO.Port=REMOTESHELL_CONTROLLER_PORT;
	if (!m_Net.Initialize(__IO))
	{
		return FALSE;
	}

	printf("-------------------Remote shell controller---------------\n");

	return TRUE;
}

void RemoteControllerFrameWork::OnCommand( char *String )
{

	if (strlen(String)==0)
	{
		return;
	}

	if(String)
	strlwr(String);
	

	m_Lexer.SortText(String);

	if (m_FSM==REMOTECONTROLLER_FSM_CONNECT)
	{
		if(strcmp(String,"exit")==0)
		{
			OnExitConnect();
			return;
		}
		OnConnectCommand(String);
	}
	else if(m_FSM==REMOTECONTROLLER_FSM_NORMAL)
	{
		OnNormalCommand(String);
	}
	else if(m_FSM==REMOTECONTROLLER_FSM_DISCONNECT)
	{
		OnDisconnectCommand(String);
	}
}

void RemoteControllerFrameWork::OnNormalCommand( char *String )
{
	CubeGrammarSentence Sen;
	unsigned int SenType;
	SenType=m_Grammer.GetNextInstr(Sen);
	if (SenType==GRAMMAR_SENTENCE_UNKNOW||SenType==GRAMMAR_SENTENCE_END)
	{
		printf("%s ����һ���Ϸ����������\n",Sen.GetBlockString(0)?Sen.GetBlockString(0):"");

		return;
	}

	if(SenType==m_list)
	{
		OnCommandList();

		return;
	}

	if (SenType==m_connect)
	{
		int Index=atoi(Sen.GetBlockString(1));

		if ((unsigned int)Index>=m_vClients.size())
		{
			printf("����ȷ�Ŀͻ�������\n");
			return;
		}

		m_CurrentOperateClient=(unsigned int)Index;

		printf("\n====================================================================\n");
		printf("RemoteShell ���ƶ�,��ǰ���ӵ� %s\n",inet_ntoa(m_vClients[Index].sin_addr));

		m_FSM=REMOTECONTROLLER_FSM_CONNECT;
		return;
	}
	printf("�޷�ʶ�������\n");
}

void RemoteControllerFrameWork::OnCommandList()
{
	m_vClients.clear();
	m_CurList=0;
	m_RecvListCount=0;
	Packet_Server_List List;
	Cube_SocketUDP_O __O;
	__O.Buffer=&List;
	__O.Size=sizeof List;
	__O.to=m_ServerAddrin;

	m_Net.Send(__O);
	m_ExecFsm=REMOTECONTROLLER_EXEC_FSM_LIST;

	if(!WaitForReply())
	{
		printf("��ȡ�б���ʱ\n");
	}
	else
	{
		m_FSM=REMOTECONTROLLER_FSM_NORMAL;
		OnListClientTable();
	}
}

void RemoteControllerFrameWork::OnExitConnect()
{
	m_CurrentOperateClient=-1;
	m_FSM=REMOTECONTROLLER_FSM_NORMAL;
}

void RemoteControllerFrameWork::OnNetRecv( Cube_SocketUDP_I & __I)
{
	if (__I.Size<sizeof Packet)
	{
		return;
	}
	if (!IsServer(__I.in))
	{
		return;
	}
	Packet *Pack=(Packet *)__I.Buffer;


	switch(Pack->TypeFLAG)
	{
	case PACKET_TYPEFLAG_SERVER_LOGINRESULT:
		{
			if(m_ExecFsm==REMOTECONTROLLER_EXEC_FSM_LOGIN)
			{
				Packet_Server_LoginReply *p=(Packet_Server_LoginReply*)__I.Buffer;
				m_ExecFsm=REMOTECONTROLLER_EXEC_FSM_NONE;
				if (p->IdentifyResult==PACKET_LOGINRESULT_FAILED)
				{
				printf("�޷�ƥ��ķ����\n");
				}
				else
				{
				printf("�ѵ�¼�������:%s\n",REMOTESHELL_SERVER_IPADDR);
				}
			}
		}
		break;
	case  PACKET_TYPEFLAG_CONTROLLER_HEARTBEAT_REPLY:
		{
			m_HeartBeat.Activate();
		}
		break;

	case  PACKET_TYPEFLAG_CONTROLLER_LIST:
		{
			Packet_Controller_List *List=(Packet_Controller_List*)__I.Buffer;
			m_RecvListCount=List->Sum;
			if (m_RecvListCount!=0)
			{
			m_CurList++;
			m_vClients.push_back(List->Addr);
			}

			if (m_CurList>=m_RecvListCount)
			{
				m_ExecFsm=REMOTECONTROLLER_EXEC_FSM_NONE;
			}
		}
		break;

	case   PACKET_TYPEFLAG_SERVER_CLIENTTRANSLATE:
		{
			Packet_Server_ClientTranslate<Packet> *Trans=(Packet_Server_ClientTranslate<Packet> *)__I.Buffer;

			if (!IsCurrentClient(Trans->ClientIn))
			{
				return;
			}

			switch(Trans->Packet.TypeFLAG)
			{
			case PACKET_TYPEFLAG_CLIENT_EXEREPLY:
				{
					if (m_ExecFsm==REMOTECONTROLLER_EXEC_FSM_CMDEXEC)
					{
						Packet_Client_ExecuteReply Rep=
							((Packet_Server_ClientTranslate<Packet_Client_ExecuteReply>*)__I.Buffer)->Packet;
						if (Rep.ExeReply==PACKET_EXEC_REPLY_SUCCEEDED)
						{
							printf("Shellִ�гɹ�\n");
						}
						if (Rep.ExeReply==PACKET_EXEC_REPLY_FAILED)
						{
							printf("Shellִ��ʧ��\n");
						}
						m_ExecFsm=REMOTECONTROLLER_EXEC_FSM_NONE;
					}
				}
				break;
			case  PACKET_TYPEFLAG_CLIENT_REPLY:
				{
					Packet_Client_Reply Rep=
						((Packet_Server_ClientTranslate<Packet_Client_Reply>*)__I.Buffer)->Packet;
					printf("\n%s\n",Rep.Reply);
				}
				break;
			}
		}
		break;
	}
}

void RemoteControllerFrameWork::OnDisconnectCommand( char *String )
{
	CubeGrammarSentence Sen;
	unsigned int SenType;
	SenType=m_Grammer.GetNextInstr(Sen);
	if (SenType==GRAMMAR_SENTENCE_UNKNOW||SenType==GRAMMAR_SENTENCE_END)
	{
		printf("%s ����һ���Ϸ����������\n",Sen.GetBlockString(0)?Sen.GetBlockString(0):"");
		return;
	}

	if(SenType==m_Login)
	{
		OnCommandLogin();
		return;
	}

	printf("�޷�ʶ�������\n");
}

void RemoteControllerFrameWork::OnCommandLogin()
{
	Packet_Server_Login Login;

	m_ExecFsm=REMOTECONTROLLER_EXEC_FSM_LOGIN;
	
	Cube_SocketUDP_O __O;
	__O.Buffer=&Login;
	__O.Size=sizeof Packet_Server_Login;
	__O.to=m_ServerAddrin;

	m_Net.Send(__O);

	printf("�������ӷ������� \"%s:%d\"\n",REMOTESHELL_SERVER_IPADDR,REMOTESHELL_SERVER_PORT);

	if(!WaitForReply())
	{
		printf("���ӳ�ʱ.....\n");
	}
	else
	{
		m_FSM=REMOTECONTROLLER_FSM_NORMAL;
		m_HeartBeat.Activate();
	}
}

void RemoteControllerFrameWork::OnDisconnectFromServer()
{
	m_FSM=REMOTECONTROLLER_FSM_DISCONNECT;
	printf("\n�ӷ���������Ѿ��Ͽ�\n");
	this->OnPrintLocation();
}

void RemoteControllerFrameWork::Run()
{
	m_HeartBeat.start();
	m_Net.start();
}

BOOL RemoteControllerFrameWork::WaitForReply()
{
	int Time=3000;
	while (m_ExecFsm!=REMOTECONTROLLER_EXEC_FSM_NONE)
	{
		Sleep(100);
		if ((Time-=100)<0)
		{
			m_ExecFsm=REMOTECONTROLLER_EXEC_FSM_NONE;
			return FALSE;
		}
	}
	
	return TRUE;
}

void RemoteControllerFrameWork::OnEmitControllerHeartbeat()
{
	Packet_Controller_HeartBeat hb;

	Cube_SocketUDP_O __O;
	__O.Buffer=&hb;
	__O.Size=sizeof Packet_Controller_HeartBeatReply;
	__O.to=m_ServerAddrin;

	m_Net.Send(__O);
}

void RemoteControllerFrameWork::OnConnectCommand( char *String )
{
	if (m_FSM!=REMOTECONTROLLER_FSM_CONNECT)
	{
		return;
	}
	if (strlen(String)>4)
	{
		char MSG[5];
		memcpy(MSG,String,4);
		MSG[4]='\0';

		if (strcmp(MSG,"msg ")==0)
		{
			Packet_Client_Message Msg;
			strcpy(Msg.message,String+4);
			Packet_Server_ControllerTranslate<Packet_Client_Message> Trans;
			Trans.ClientIn=m_vClients[m_CurrentOperateClient];
			Trans.Packet=Msg;

			Cube_SocketUDP_O __O;
			__O.Buffer=&Trans;
			__O.Size=sizeof Trans;
			__O.to=m_ServerAddrin;

			m_Net.Send(__O);
			return;
		}
	}

	
	Packet_Client_CMD CMD;
	strcpy(CMD.command,String);
	strcat(CMD.command,"\r\n");
	Packet_Server_ControllerTranslate<Packet_Client_CMD> Trans;
	Trans.ClientIn=m_vClients[m_CurrentOperateClient];
	Trans.Packet=CMD;

	Cube_SocketUDP_O __O;
	__O.Buffer=&Trans;
	__O.Size=sizeof Trans;
	__O.to=m_ServerAddrin;

	m_Net.Send(__O);
	
	m_ExecFsm=REMOTECONTROLLER_EXEC_FSM_CMDEXEC;

	if(!WaitForReply())
	{
		m_ExecFsm=REMOTECONTROLLER_EXEC_FSM_NONE;
		printf("Shellִ�г�ʱ\n");
	}
}

void RemoteControllerFrameWork::OnPrintLocation()
{
	switch (m_FSM)
	{
	case REMOTECONTROLLER_FSM_CONNECT:
		{
			printf("%s:\\>",inet_ntoa(m_vClients[m_CurrentOperateClient].sin_addr));
		}
		break;
	case REMOTECONTROLLER_FSM_DISCONNECT:
		{
			printf("Local:\\>");
		}
		break;
	case REMOTECONTROLLER_FSM_NORMAL:
		{
			printf("Server:\\>");
		}
		break;
	}
}

BOOL RemoteControllerFrameWork::IsServer( SOCKADDR_IN in )
{
	if (in.sin_addr.S_un.S_addr==m_ServerAddrin.sin_addr.S_un.S_addr)
	{
		if (in.sin_port==m_ServerAddrin.sin_port)
		{
			return TRUE;
		}
	}
	return FALSE;
}

void RemoteControllerFrameWork::OnListClientTable()
{
	if(m_vClients.size()==0)
	{
			printf("�����߿ͻ���\n");
			return;
	}

	for (unsigned int i=0;i<m_vClients.size();i++)
	{
		printf("%d:\tClient On:%s:%d\n",i,inet_ntoa(m_vClients[i].sin_addr),m_vClients[i].sin_port);
	}
}

BOOL RemoteControllerFrameWork::IsCurrentClient( SOCKADDR_IN in )
{
	if (m_FSM!=REMOTECONTROLLER_FSM_CONNECT)
	{
		return FALSE;
	}
	if (m_CurrentOperateClient==-1||m_CurrentOperateClient>=m_vClients.size())
	{
		return FALSE;
	}
	if (in.sin_addr.S_un.S_addr==m_vClients[m_CurrentOperateClient].sin_addr.S_un.S_addr)
	{
		if (in.sin_port==m_vClients[m_CurrentOperateClient].sin_port)
		{
			return TRUE;
		}
	}
	return FALSE;
}

void RemoteControllerHeartbeat::run()
{
	while(TRUE)
	{	
		Sleep(100);

		if(G_RemoteFrameWork.m_FSM!=REMOTECONTROLLER_FSM_DISCONNECT)
		{
		if(m_Time>0)
		{
			m_Time-=100;
			if (m_Time<=0)
			{
				G_RemoteFrameWork.OnDisconnectFromServer();
			}
		}

		G_RemoteFrameWork.OnEmitControllerHeartbeat();
		}
	}
	
}

void RemoteControllerHeartbeat::Activate()
{
	m_Time=5000;
}