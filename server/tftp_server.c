#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#define BUFFER_SIZE 1024
#define FILE_BUFFER_SIZE 512
#define PACKET_SIZE 516
#define REQ_SIZE 516
#define ACK_SIZE 4
#define IP_SIZE 16

int create_error(uint16_t, char* , const char*);
int create_text_pckt(uint16_t, char*, FILE*, unsigned int);
int create_bin_pckt(uint16_t, char*, FILE*, unsigned int);

struct read_request {
	int sd;
	struct sockaddr_in client_addr;
	FILE* fp;
	int remaining_pckts;
	char* mode;
	int block;
	struct read_request* next;
} rr_list;

void init_rr_list();
void add_rr(int, struct sockaddr_in, FILE*, int, char*, int);
void remove_rr(int);
struct read_request* findRequest(int);


int main(int argc, char** argv)
{

	if(argc != 3)
	{
		printf("\nPer avviare il programma digita ./tftp_server <porta> <directory files>\n\n");
		return 0;
	}
	
	int port = atoi(argv[1]);
	char* dir = argv[2]; //Directory dei file


	fd_set read_fds;
	fd_set master;
	int fdmax, newfd;
	

	int listener, ret, buffer_index, i;
	unsigned int addrlen;
	struct sockaddr_in my_addr, client_addr;
	

	//Creazione socket di ascolto
	listener = socket(AF_INET, SOCK_DGRAM,0);

	//Creazione indirizzo di bind
	memset(&my_addr, 0, sizeof(my_addr)); //Pulisco la struttura
	my_addr.sin_family = AF_INET;
	my_addr.sin_port = htons(port);
	my_addr.sin_addr.s_addr = INADDR_ANY;
	printf("\nSocket listener creato.\n");

	ret = bind(listener, (struct sockaddr*)&my_addr, sizeof(my_addr));
	if(ret < 0)
	{
		perror("ERRORE: Bind non riuscito");
		exit(0);
	}

	//Reset FD
	FD_ZERO(&master);
	FD_ZERO(&read_fds);

	//Aggiungo il socket del listener al set dei descrittori
	FD_SET(listener, &master);
	fdmax = listener;

	char fileName[BUFFER_SIZE];
	char mode[BUFFER_SIZE];	
	char buffer_pckt[PACKET_SIZE];
	char buffer_error[BUFFER_SIZE];
	char ip_client[IP_SIZE];

	init_rr_list();
	while(1)
	{
		//Reset del set
		read_fds = master;
		//Si blocca in attesa di descrittori pronti
		select(fdmax+1, &read_fds, NULL, NULL, NULL);
		
		for(i = 0; i <= fdmax; i++)
		{
			// printf("[DEBUG]: descrittore num: %d\n", i);
			if(FD_ISSET(i, &read_fds))
			{
				// printf("\n[DEBUG]: il descrittore %d è settato.\n", i);
				if(i == listener)
				{
					addrlen = sizeof(client_addr);
					memset(buffer_pckt, 0, BUFFER_SIZE);
					// printf("\n[DEBUG]: descrittore socket: %d\n", i);

					ret = recvfrom(i, buffer_pckt, REQ_SIZE, 0, (struct sockaddr*)&client_addr, &addrlen);
					// printf("\n[DEBUG]: byte della req ricevuti dal client: %d\n", ret);
					if(ret < 0)
					{
						perror("Errore nella receive.\n");
						exit(0);
					}

					uint16_t opcode, err_code;
					memcpy(&opcode, buffer_pckt, 2);
				
					opcode = ntohs(opcode);
					// printf("\n[DEBUG]: Opcode: %d\n", opcode);

					// Errore
					if(opcode != 1 && opcode != 4)
					{
						buffer_index = 0;
						char *err_msg = "Operazione non consentita.\n";
						err_code = htons(2);


						memset(buffer_error, 0, BUFFER_SIZE);
						buffer_index = create_error(err_code, buffer_error, err_msg);

						newfd = socket(AF_INET, SOCK_DGRAM, 0);

						ret = sendto(newfd, buffer_error, buffer_index, 0,(struct sockaddr*)&client_addr, sizeof(client_addr));
						if(ret < 0)
						{
							perror("\n[ERRORE]: Invio dei dati non riuscito\n");
							exit(0);
						}
						close(newfd);
						
						continue;
					}

					if(opcode == 1) //Se c'è una richiesta dal client  provo a leggere il file
					{
						newfd =  socket(AF_INET, SOCK_DGRAM, 0);

						buffer_index = 0;
				
						memset(fileName, 0, BUFFER_SIZE);
						strcpy(fileName, buffer_pckt+2);
						strcpy(mode, buffer_pckt + (int)strlen(fileName) + 3); //3= 2byte per opcode + 1 byte per 0x00

						memset(ip_client, 0, IP_SIZE);
						inet_ntop(AF_INET, &client_addr, ip_client, IP_SIZE);

						printf("\nRichiesta di download del file %s in modalità %s da %s\n", fileName, mode, ip_client);
				
						char* path = malloc(strlen(dir)+strlen(fileName)+2);
						strcpy(path, dir);
						strcat(path, "/");
						strcat(path, fileName);

						// printf("\n[DEBUG]: lettura file: %s\n", fileName);
						FILE* fp;
						if(!strcmp(mode, "netascii\0"))
							fp = fopen(path, "r");
						else
							fp = fopen(path, "rb");

						free(path);

						if(fp == NULL)
						{
							char *err_msg = "File non trovato";
							buffer_index = 0;
							err_code = htons(1);

							memset(buffer_error, 0, BUFFER_SIZE);
							buffer_index = create_error(err_code, buffer_error, err_msg);

						
							newfd = socket(AF_INET, SOCK_DGRAM, 0);
			
							// printf("\n[DEBUG]: Newfd: %d\n", newfd);

							ret = sendto(newfd, buffer_error, buffer_index, 0,(struct sockaddr*)&client_addr, sizeof(client_addr));

							if(ret < 0)
							{
								perror("\n[ERRORE]: Invio dei dati non riuscito\n");
								exit(0);
							}

							close(newfd);
							continue;
						}
							else
						{
							printf("\nLettura del file %s riuscita\n", fileName);
			
							if(!strcmp(mode, "netascii\0"))
							{	
								// Lettura della lunghezza del contenuto del file	
								unsigned int length = 0;
								while(fgetc(fp) != EOF)
									length++;
								// printf("\n[DEBUG]: lunghezza contenuto file: %d\n", (int)length);

								//Reimposta l'indicatore di posizione del file all'inizio dello stesso
								fseek(fp, 0 , SEEK_SET);

								unsigned int dim_pckt = (length > FILE_BUFFER_SIZE)?FILE_BUFFER_SIZE:length;
							
								add_rr(newfd, client_addr, fp, length-dim_pckt, mode, 0);	

								// Lettura ed invio di un blocco
							
								uint16_t block_num = htons(1);


								buffer_index = create_text_pckt(block_num, buffer_pckt, fp, dim_pckt);

								// printf("\n[DEBUG]: dimensione invio: %d\n", buffer_index);

								ret = sendto(newfd, buffer_pckt, buffer_index, 0, (struct sockaddr*)&client_addr, sizeof(client_addr));

								if(ret < 0) 
								{
									perror("[ERRORE]: errore durante la send del blocco al client.");
									exit(0);
								}

								// printf("\n[DEBUG]:Invio del blocco [0].\n");
			
						
							}
							else //modalità bin
							{
								// Lettura della lunghezza del contenuto del file

								//Imposta l'indicatore di posizione del file alla fine dello stesso
								fseek(fp, 0 , SEEK_END);
								//Ritorna la posizione corrente nel file
								unsigned int length = ftell(fp);
								//Resetto l'indicatore
								fseek(fp, 0 , SEEK_SET);
								unsigned int dim_pckt = (length > FILE_BUFFER_SIZE)?FILE_BUFFER_SIZE:length;
								add_rr(newfd, client_addr, fp, length-dim_pckt, mode, 0);

								uint16_t block_num = htons(1);

								buffer_index = create_bin_pckt(block_num,buffer_pckt, fp, dim_pckt);

								ret = sendto(newfd, buffer_pckt, buffer_index, 0, (struct sockaddr*)&client_addr, sizeof(client_addr));

								if(ret < 0)
								{
									perror("Errore nella send");
									exit(0);
								}
						
							}

							FD_SET(newfd, &master); //Aggiungo la richiesta al set
							// printf("\n[DEBUG]: setto il descrittore %d.\n", newfd);
							if(newfd > fdmax) 
								fdmax = newfd;	
						}
					}

				} 
					else
				{
					addrlen = sizeof(client_addr);
					memset(buffer_pckt, 0, BUFFER_SIZE);
					//So che la dimensione dell'ACK è fissa
					ret = recvfrom(i, buffer_pckt, ACK_SIZE, 0, (struct sockaddr*)&client_addr, &addrlen);
					// printf("\n[DEBUG]: byte della ack ricevuti dal client: %d\n", ret);
					if(ret < 0)
					{
						perror("Errore nella receive.\n");
						exit(0);
					}

					uint16_t opcode;
					memcpy(&opcode, buffer_pckt, 2);
				
					opcode = ntohs(opcode);
					// printf("\n[DEBUG]: Opcode: %d\n", opcode);

					if(opcode == 4) //Se abbiamo ricevuto un ACK
					{
						// printf("\n[DEBUG]: ACK ricevuto.\n");
						
						struct read_request* r = findRequest(i);

						// printf("\n[DEBUG]: numero byte rimanenti: %d\n", r->remaining_pckts);

						if(r->remaining_pckts > 0)
						{ 
							unsigned int dim_pckt = (r->remaining_pckts > FILE_BUFFER_SIZE)?FILE_BUFFER_SIZE:r->remaining_pckts;
							r->block++;

							if(r->remaining_pckts == FILE_BUFFER_SIZE)
								r->remaining_pckts = 1; //Invio un pacchetto vuoto quando è multiplo di 512
							else
								r->remaining_pckts -= dim_pckt;
							// printf("\r[DEBUG]: Invio del blocco [%d]\n", r->block);	
				
							
							// Lettura ed invio di un blocco
							uint16_t block_num = htons(r->block);

							if(!strcmp(r->mode, "netascii\0")) //Modalità testo
							{
								buffer_index = create_text_pckt(block_num, buffer_pckt, r->fp, dim_pckt);

								// printf("\n[DEBUG]: dimensione invio blocco[%d]: %d\n", r->block, buffer_index);
							}
								else //Modalità bin
							{
								buffer_index = create_bin_pckt(block_num, buffer_pckt, r->fp, dim_pckt);
							}

							ret = sendto(i, buffer_pckt, buffer_index, 0, (struct sockaddr*)&r->client_addr, sizeof(r->client_addr));
							memset(buffer_pckt, 0, FILE_BUFFER_SIZE);

						}
							else //Tutto il file è stato trasmesso
						{
							memset(ip_client, 0, IP_SIZE);
							inet_ntop(AF_INET, &r->client_addr, ip_client, IP_SIZE);
							printf("\nL'intero file è stato trasferito con successo al client %s\n", ip_client);
							remove_rr(i);
							close(i);
							FD_CLR(i, &master);
						}	
					}
				}
			}
		}
	}
	close(listener);

}




int create_error(uint16_t error_code, char* buffer_error, const char* error_msg)
{
	printf("\n%s\n", error_msg);

	//Creo il messaggio di errore
	uint16_t opcode = htons(5);
	int buffer_index = 0;
	
	memcpy(buffer_error, (uint16_t*)&opcode, 2);
	buffer_index += 2;

	memcpy(buffer_error+buffer_index, (uint16_t*)&error_code, 2);
	buffer_index += 2;

	strcpy(buffer_error+buffer_index, error_msg);
	buffer_index += strlen(error_msg)+1;
	buffer_index++; //0x00 finale

	return buffer_index;
}


int create_text_pckt(uint16_t block_num, char* buffer_pckt, FILE* fp, unsigned int dim_pckt)
{
	uint16_t opcode = htons(3);
	int buffer_index = 0;
	char buffer_file[FILE_BUFFER_SIZE];
	memset(buffer_file, 0, FILE_BUFFER_SIZE);

	fread(buffer_file, dim_pckt, 1, fp);
	// printf("\n[DEBUG]: dati letti: %s\n", buffer_file);

	memcpy(buffer_pckt + buffer_index, (uint16_t*)&opcode, 2);
	buffer_index += 2;
	memcpy(buffer_pckt + buffer_index, (uint16_t*)&block_num, 2);
	buffer_index += 2;
	strcpy(buffer_pckt + buffer_index, buffer_file);
	buffer_index += dim_pckt;

	return buffer_index;
}


int create_bin_pckt(uint16_t block_num, char* buffer_pckt, FILE* fp, unsigned int dim_pckt)
{
	u_int16_t opcode = htons(3);
	int buffer_index = 0;
	char buffer_file[FILE_BUFFER_SIZE];
	memset(buffer_file, 0, FILE_BUFFER_SIZE);	
	fread(buffer_file, dim_pckt, 1, fp);

	memcpy(buffer_pckt, (uint16_t*)&opcode, 2);
	buffer_index += 2;
	memcpy(buffer_pckt + buffer_index, (uint16_t*)&block_num, 2);
	buffer_index += 2;

	memcpy(buffer_pckt + buffer_index, buffer_file, dim_pckt);
	buffer_index += dim_pckt;

	return buffer_index;
}



void init_rr_list()
{
	rr_list.sd = -1;
	rr_list.fp = NULL;
	rr_list.remaining_pckts = 0;
	rr_list.next = NULL;
}

void add_rr(int sock, struct sockaddr_in client_addr, FILE* fp, int packets,char* mode, int block)
{
	struct read_request *r = malloc(sizeof(struct read_request));
	r->sd = sock;
	r->client_addr = client_addr;
	r->fp = fp;
	r->remaining_pckts = packets;
	r->mode = malloc(sizeof(mode)+1);
	strcpy(r->mode, mode);
	r->block = block;
	r->next = NULL;

	if(rr_list.next == NULL)
	{
		rr_list.next = r;
		return;
	}

	struct read_request* prec = rr_list.next;
	struct read_request* tmp = NULL;
	while(prec != NULL)
	{
		tmp = prec;
		prec = prec->next;
	}

	tmp->next = r;
	return;
}



void remove_rr(int sock)
{
	struct read_request *r = rr_list.next;
	struct read_request* prec = NULL;

	while(r)
	{
		if(sock == r->sd)
			break;
		prec = r;
		r = r-> next;
	}

	// printf("\n[DEBUG]: chiusura file\n");
	fclose(r->fp);

	if(r)
	{
		if(r == rr_list.next)
			rr_list.next = r->next;
		else
			prec->next = r->next;
		
	}
}

struct read_request* findRequest(int sock)
{
	struct read_request* r = &rr_list;
	while(r)
	{
		if(r->sd == sock)
			break;
		r = r->next;
	}
 return r;
}
