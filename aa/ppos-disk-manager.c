#include <signal.h>
#include <errno.h>
#include <string.h>
#include "ppos.h"
#include "ppos-core-globals.h"
#include "disk-driver.h"
#include "ppos-disk-manager.h"

// adicione todas as variaveis globais necessarias para implementar o gerenciado do disco

// Variáveis globais
task_t disk_manager_task;
diskrequest_t *request_queue = NULL;
int disk_busy = 0;
int numBlocks, blockSize;

diskrequest_t *queue_remove();
void disk_mgr_signal_handler(int signum);
void bodyDiskManager(void* arg);
diskrequest_t* disk_scheduler(diskrequest_t* queue);


// Função da tarefa gerenciadora do disco
void bodyDiskManager(void* arg) {
    while (1) { //O loop infinito garante que o gerenciador de disco nunca pare de funcionar enquanto houver pedidos de leitura/escrita a serem atendidos.
        if (request_queue) { //Verifica se há pedidos na fila , se houver solicitações de leitura ou escrita, o gerenciador deve processá-las.
            diskrequest_t *req = queue_remove();  //Remove a primeira solicitação da fila (request_queue) e armazena a referência em req. Queue_remove() segue a política FCFS (First Come, First Served), garantindo que os pedidos sejam atendidos na ordem de chegada.

            disk_busy = 1; // Define disk_busy = 1, indicando que o disco está ocupado processando uma solicitação. Isso evita que outras requisições sejam tratadas simultaneamente, pois o disco só pode processar um pedido por vez.
            if (req->operation == 0) { //Verifica se a operação solicitada é leitura (0) ou escrita (1).
                disk_cmd(DISK_CMD_READ, req->block, req->buffer);  // le o bloco de disco e armazena no buffer
            } else {
                disk_cmd(DISK_CMD_WRITE, req->block, req->buffer); //gravar os dados no bloco de disco
            }
            task_suspend(req->task,    ); //Suspende a tarefa solicitante até que a operação de leitura/escrita seja concluída.
        } else {
            task_suspend(NULL,   ); //Se não houver solicitações na fila, a própria tarefa gerenciadora do disco (disk_manager_task) será suspensa. Isso impede que o gerenciador fique consumindo CPU à toa quando não há nada para processar.
        }
    }
}
void diskSignalHandler(int signum) // Essa função é essencial para garantir que as operações sejam executadas sequencialmente e que as tarefas aguardem corretamente a finalização das operações de I/O no disco.
{
    disk_busy = 0; //indica que o disco não está mais ocupado.
    task_resume(&disk_manager_task); //processa o próximo pedido.
}

// Adiciona uma solicitação na fila
diskrequest_t *queue_add(task_t *task, int op, int block, void *buffer) { //retorna um ponteiro para disk_request_t, que representa um pedido de leitura/escrita no disco.
    diskrequest_t *new_req = (diskrequest_t *)malloc(sizeof(diskrequest_t)); //Aloca memória dinamicamente para um novo pedido de disco
    new_req->task = task;
    new_req->operation = op;
    new_req->block = block;
    new_req->buffer = buffer;
    new_req->next = NULL; // Inicialmente NULL porque será o último item da fila.

    if (!request_queue) {//Se a fila está vazia (NULL), o novo pedido se torna o primeiro da fila.
        request_queue = new_req;  
    } else { //Se a fila já contém pedidos: 
        diskrequest_t *aux = request_queue;
        while (aux->next) {    //Percorre a lista até encontrar o último elemento.
            aux = aux->next;
        }
        aux->next = new_req; //Adiciona new_req ao final da fila (aux->next = new_req), garantindo que os pedidos sejam processados em ordem FCFS (First Come, First Served).

    }
    return new_req; //Retorna um ponteiro para a nova requisição adicionada à fila.
}

// Remove o primeiro pedido da fila
diskrequest_t *queue_remove() {
    if (!request_queue) return NULL;
    diskrequest_t *req = request_queue; //Armazena um ponteiro para o primeiro elemento da fila (o pedido que será removido).
    request_queue = request_queue->next; 
    return req;
}

// função para o tratamento de erros dos sinais - usada em disk_mgr_init()
//Isso significa que, se o programa acessar memória inválida (causando SIGSEGV), clean_exit_on_sig será chamada automaticamente para reportar e encerrar o programa.
void clean_exit_on_sig(int sig_num)  
{
    printf ("\n ERROR[Signal = %d]: %d \"%s\"", sig_num, errno, strerror(errno));
    exit(errno);
}

int disk_mgr_init (int *numBlocks, int *blockSize) {

    if (disk_cmd(DISK_CMD_INIT, 0, 0) < 0) {   //Inicializa o disco virtual
        return -1;
    }
    numBlocks = disk_cmd(DISK_CMD_DISKSIZE, 0, 0);
    blockSize = disk_cmd(DISK_CMD_BLOCKSIZE, 0, 0);

    task_create(&disk_manager_task, bodyDiskManager, NULL);  //Cria a tarefa gerenciadora do disco, que será responsável por processar os pedidos de leitura/escrita.
    signal(SIGUSR1, disk_mgr_signal_handler); //Registra um manipulador de sinal para SIGUSR1. Quando o disco virtual terminar uma operação de leitura/escrita, ele enviará SIGUSR1, e a função disk_mgr_signal_handler será chamada.
    signal(SIGSEGV, clean_exit_on_sig); //Registra um manipulador de erro para SIGSEGV (falha de segmentação). Se o programa tentar acessar memória inválida, a função clean_exit_on_sig será chamada, imprimindo um erro e encerrando o programa com segurança.

    // o seu codigo deve terminar ate aqui. 
    // As proximas linhas dessa função não devem ser modificadas
    return 0;
}
// Leitura de um bloco do disco
int disk_block_read(int block, void* buffer) {  //Cria uma requisição de leitura e a adiciona à fila.
    
 if (block < 0 || block >= numBlocks) return -1;
    diskrequest_t *req = queue_add(task_get_current(), 0, block, buffer);
    task_suspend(req->task);
    return 0;
}
// Escrita de um bloco no disco
int disk_block_write(int block, void* buffer) { //Cria uma requisição de escrita e a adiciona à fila.

    if (block < 0 || block >= numBlocks) return -1;
    diskrequest_t *req = queue_add(task_get_current(), 1, block, buffer);
    task_suspend(req->task);

    return 0;
}


// Essa função implemeneta o escalonador de requisicoes de 
// leitura/scrita do disco usado pelo gerenciador do disco
// A função implementa a política FCFS.
diskrequest_t* disk_scheduler(diskrequest_t* queue) {
     // FCFS scheduler
    if ( queue != NULL ) {
        PPOS_PREEMPT_DISABLE
        diskrequest_t* request = queue;
        PPOS_PREEMPT_ENABLE
        return request;
    }
    return NULL;
}