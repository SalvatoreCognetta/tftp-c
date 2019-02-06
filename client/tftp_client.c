#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define BUFFER_SIZE 1024
#define FILE_BUFFER_SIZE 512
#define PACKET_SIZE 516


#define CMD_SIZE 50

void initMessage(int, const char*, int, struct sockaddr_in);
void help();
void get(int, char*, char*, char*, struct sockaddr_in);
void mode(char*, char*);
void quit();


int main(int argc, char** argv)
{
	if(argc != 3)
	{
		printf("\nPer avviare correttamente il programma digita: ./tftp_client <IP_server> <porta_server>\n");
		return 0;
	}

	int sd, port;
	struct sockaddr_in server_addr;

	char input_cmd[CMD_SIZE];
	

	port = atoi(argv[2]);

	//Creazione socket
	sd = socket(AF_INET, SOCK_DGRAM, 0);


	//Creazione indirizzo del server
	memset(&server_addr, 0, sizeof(server_addr)); //Pulisco la struttura
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	inet_pton(AF_INET, argv[1], &server_addr.sin_addr);


	char transfer_mode[CMD_SIZE]; 
	//Di default il modo di trasferimento è binario (octet = binary, netascii = text)
	strcpy(transfer_mode,"octet\0");

	initMessage(sd, argv[1], port, server_addr);

	while(1)
	{
		
		memset(&input_cmd, 0, CMD_SIZE); //Pulisco il buffer
		printf("\nInserisci il comando: >");

		fgets(input_cmd, CMD_SIZE, stdin);
		//strtok effettua lo split di una stringa in base ad un delimitatore specificato
		char *cmd = strtok(input_cmd, "\n");  //Elimino il carattere a capo
		cmd = strtok(cmd, " ");

		// scanf("%s", cmd);

		if(!strcmp(cmd, "!help\0"))
		{
			help();
		} 
			else if(!strcmp(cmd, "!mode\0"))
		{
			//Nel buffer input_cmd viene immagazzinato anche il carattere di new-line \n dalla fgets()
			char *new_mode = strtok(NULL, "\n");

			//La strtok restituisce NULL se non ci sono ulteriori 'token'
			if(new_mode == NULL)
			{
				printf("[WARNING]: Modo di trasferimento non specificato, inserire {txt|bin}.\n");
				continue;
			}

			mode(new_mode, transfer_mode);
		} 
			else if(!strcmp(cmd, "!get\0"))
		{
			// char fileName[BUFFER_SIZE], local_name[BUFFER_SIZE];
			// memset(&fileName, 0, BUFFER_SIZE);
			// memset(&localName, 0, BUFFER_SIZE);
			// scanf("%s %s", fileName, localName);

			char *file_name = strtok(NULL, " ");
			char *local_name = strtok(NULL, "\n");
			if(local_name == NULL)
			{
				printf("[WARNING]: filename o nome_locale non specificato, inserire {filename + nome_locale}.\n");
				continue;
			}
			get(sd, file_name, local_name, transfer_mode, server_addr);
		}
			else if(!strcmp(cmd, "!quit\0"))
		{
			quit(sd);
		} 
			else
		{
			printf("\nOperazione non prevista, digita !help per la lista dei comandi\n");	
		}
	}
 return 0;
}



void initMessage(int sd, const char* server, int port, struct sockaddr_in server_addr)
{
	printf("Stai comunicando con %s alla porta %d effettuata con successo\n", server,port);
	help();
}

void help()
{
	printf("\nSono disponibili i seguenti comandi:\n"
				"!help --> mostra l'elenco dei comandi disponibili\n"
				"!mode {txt|bin} --> imposta il modo di trasferimento dei files (testo o binario)\n"
				"!get filename nome_locale --> richiede al server il nome del file <filename> e lo salva localmente con il nome <nome_locale>\n"
				"!quit --> termina il client\n");
}

void mode(char* new_mode, char* current_mode){
	if(!strcmp(new_mode, "txt"))
	{	
		printf("Modo di trasferimento testuale configurato.\n");
		strcpy(current_mode,"netascii\0");
	} 
		else if(!strcmp(new_mode, "bin"))
	{
		printf("Modo di trasferimento binario configurato.\n");
		strcpy(current_mode,"octet\0");
	} 
		else 
	{
		printf("[WARNING]: Modo di trasferimento non previsto, inserire {txt|bin}.\n");
	}
	return;	
}

void get(int sd, char* file_name, char* local_name, char* transfer_mode, struct sockaddr_in server_addr)
{
	FILE* fp;
	if(!strcmp(transfer_mode, "netascii\0"))
	{
		fp = fopen(local_name, "w+");
	} else {
		fp = fopen(local_name, "wb+");
	}

	if(fp == NULL)
	{
		printf("\nErrore nell'apertura del file.\n");
		return;
	}

	printf("\nRichiesta file %s al server in corso.\n", file_name);

	unsigned int addrlen = sizeof(server_addr);

	char rrq[BUFFER_SIZE];//Buffer per il messaggio RRQ
	memset(&rrq, 0, BUFFER_SIZE); //Pulisco il buffer
	
	uint16_t opcode = htons(1); //OPCODE 1=RRQ, 2=WRQ


	uint16_t file_name_len = strlen(file_name);

	int buffer_index = 0; //Indice del pacchetto
	long long transfers = 0;  //Numero di blocchi trasferiti
	
	//Creo il messaggio di richiesta RRQ
	memcpy(rrq, (uint16_t*)&opcode, 2);
	buffer_index += 2;

	strcpy(rrq + buffer_index, file_name);
	buffer_index += file_name_len + 1; 

	strcpy(rrq + buffer_index, transfer_mode);
	buffer_index += strlen(transfer_mode)+1;
	
	//Invio la richiesta RRQ

	int ret = sendto(sd, rrq, buffer_index, 0,(struct sockaddr*)&server_addr, sizeof(server_addr));

	if(ret < 0)
	{
		perror("[ERRORE]: send errata.\n");
		exit(0);
	}

	char file[BUFFER_SIZE];
	char ack_packet[BUFFER_SIZE];

	
	// printf("\n[DEBUG]: localname: %s\n", local_name);

	while(1)
	{			
		char buffer_pckt[BUFFER_SIZE];
		memset(&buffer_pckt, 0, BUFFER_SIZE);	
	
		// Ricezione del blocco	
		buffer_index = 0;
	
		memset(&file, 0, FILE_BUFFER_SIZE); //Pulisco il buffer

		// printf("\n[DEBUG]: attesa ricezione buff.\n");
		int byte_rcv = recvfrom(sd, buffer_pckt, PACKET_SIZE, 0, (struct sockaddr*)&server_addr, &addrlen);
		// printf("\n[DEBUG]:Byte ricevuti: %d\n", byte_rcv);
		if(byte_rcv < 0) 
		{
			perror("[ERRORE]: errore durante la ricezione dei dati.");
			exit(0);
		}


		memcpy(&opcode, buffer_pckt, 2);
		opcode = ntohs(opcode);
		buffer_index += 2;

		// Errore
		if(opcode == 5)
		{ 
			uint16_t error_number;

			memcpy(&error_number, buffer_pckt+buffer_index, 2);
			
			error_number = ntohs(error_number);
			buffer_index += 2;

			char error_msg[BUFFER_SIZE];
			memset(&error_msg, 0, BUFFER_SIZE);  //Pulisco
			strcpy(error_msg, buffer_pckt+buffer_index);

			printf("\n[ERRORE]: (%d) %s\n",error_number, error_msg);
			remove(local_name);  //Elimina local_name
			return;
		}


		if(transfers == 0)
			printf("\nTrasferimento del file in corso.\n");
		// Lettura del blocco
		uint16_t block_number;
		memcpy(&block_number, buffer_pckt + buffer_index, 2);
		
		block_number = ntohs(block_number);
		buffer_index += 2;
		
		if(!strcmp(transfer_mode, "netascii\0"))
		{
			//Text Protocol
			strcpy(file, buffer_pckt+buffer_index);
			// printf("\n[DEBUG]: dim file: %d \t file: %s\n", (int)strlen(file), file);
			fprintf(fp, "%s", file);
			// printf("\n[DEBUG]: trasferimento modalità text.\n");

		} 
			else 
		{
			// printf("\n[DEBUG]: trasferimento modalità bin.\n");
			//Binary protocol
			memcpy(file, buffer_pckt+buffer_index, FILE_BUFFER_SIZE);
			// printf("\n[DEBUG]: file: \n");

			fwrite(&file, byte_rcv-4, 1 ,fp); // -4 per eliminare opcode e block_number
		}

		
		// Invio dell'ACK	
		buffer_index = 0;
		
		memset(ack_packet, 0, BUFFER_SIZE);
		
		//Copio l'Opcode 4=ACK
		opcode = htons(4);
		memcpy(ack_packet, (uint16_t*)&opcode, 2);
		buffer_index += 2;


		//Copio il numero del blocco
		block_number = htons(block_number);
		memcpy(ack_packet + buffer_index, (uint16_t*)&block_number, 2);
		buffer_index += 2;
		// printf("\n[DEBUG]: dimensione ACK: %d.\n", buffer_index);
		
		// printf("\rScaricando...");
		ret = sendto(sd, ack_packet, buffer_index, 0,(struct sockaddr*)&server_addr, sizeof(server_addr));
		// printf("\n[DEBUG]: invio ACK.\n");

		transfers++;
		// printf("\n[DEBUG]: blocco atteso: %d\n", (int)transfers);
			
		// Fine dei trasferimenti
		if(byte_rcv < PACKET_SIZE)
		{
			printf("\nTrasferimento del file completato (%llu/%llu blocchi).\n", transfers, transfers);
			printf("\nSalvataggio %s completato.\n", file_name);
			fclose(fp);
			break;
		}
	}
 
}

void quit(int sd)
{
	close(sd);
	printf("\nDisconnessione effettuata.\n");
	exit(0);
}



