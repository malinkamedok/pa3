#include "banking.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include "pipes.h"
#include "pa2345.h"
#include "ipc.h"
#include "balance.h"
#include "lamport.h"

int main(int argc, char * argv[]) {

    if (strcmp(argv[1], "-p") != 0) {
        return -1;
    }

    int number_of_child_procs = atoi(argv[2]);
    if (number_of_child_procs == 0) {
        return -1;
    }

    if (number_of_child_procs < 1 || number_of_child_procs > 10) {
        return -1;
    }

    if (argc < 4 || argc > 13) {
        return -1;
    }

    if (argc-3 != number_of_child_procs) {
        return -1;
    }

    int* money_at_start  = (int*)calloc(number_of_child_procs, sizeof(int));

//    printf( "Size = %d\n", number_of_child_procs ) ;

    for( int i = 3; i < argc; i++)
    {
        sscanf( argv[i], "%d", &money_at_start[i-3] ) ;
    }

//    for (int i = 0; i < number_of_child_procs; i++) {
//        printf("child number %d has money %d\n", i+1, money_at_start[i]);
//    }

    FILE *pi;
    pi = fopen(pipes_log, "a+");

    FILE *ev;
    ev = fopen(events_log, "a+");

    pipes_all_global * global = new(number_of_child_procs+1);

    //pipes creation
    for (size_t i = 0; i < number_of_child_procs + 1; i++) {
        for (size_t j = 0; j < number_of_child_procs + 1; j++) {
            if (i != j) {
                int result_pipe = pipe(global->pipes_all[i][j].fd);
                fprintf(pi, "pipe opened i: %zu j: %zu\n", i, j);
                fcntl(global->pipes_all[i][j].fd[0], F_SETFL, O_NONBLOCK);
                fcntl(global->pipes_all[i][j].fd[1], F_SETFL, O_NONBLOCK);
                if (result_pipe == -1) {
                    return -1;
                }
            }
        }
    }

    init_lamport_time(global);

//    printf("lamport: %d\n", read_lamport_time(global));

    size_t process_number = 0;
    size_t len = 27;

    //procs creation
    for (size_t count = 1; count < number_of_child_procs + 1; count++) {
        if (fork() == 0) {
            process_number = count;

            //increase before sending started message
            incr_lamport_time(global);

            printf(log_started_fmt, read_lamport_time(global), (int ) process_number, getpid(), getppid(), money_at_start[count-1]);

            fprintf(ev, log_started_fmt, read_lamport_time(global), (int ) process_number, getpid(), getppid(), money_at_start[count-1]);

            //CLOSING PIPES FOR CHILD PROC
            // close pipes not working with current process
            for (size_t i = 0; i < number_of_child_procs+1; i++) {
                for (size_t j = 0; j < number_of_child_procs+1; j++) {
                    if (i != j && i != process_number && j != process_number) {
                        close(global->pipes_all[i][j].fd[0]);
                        close(global->pipes_all[i][j].fd[1]);
                        close(global->pipes_all[j][i].fd[0]);
                        close(global->pipes_all[j][i].fd[1]);
                    }
                }
            }
            for (size_t i = 0; i < number_of_child_procs+1; i++) {
                if (i != process_number) {
                    close(global->pipes_all[process_number][i].fd[0]);
                    close(global->pipes_all[i][process_number].fd[1]);
                }
            }
            baby_maybe_process * babyMaybeProcess = new_baby(process_number, global);

            BalanceHistory * new_history = (BalanceHistory*) malloc(sizeof (BalanceHistory));



            for (size_t i = 0; i < len; i++) {
                BalanceState * balanceState = new_balance(money_at_start[process_number-1], get_physical_time()+i, 0);

                new_history->s_history_len = len;
                new_history->s_id = process_number;
                new_history->s_history[get_physical_time()+i] = *balanceState;
            }

//            BalanceState * balanceState3 = new_balance(money_at_start[process_number-1], 1, 0);
//            new_history->s_history_len = 16;
//            new_history->s_id = process_number;
//            new_history->s_history[1] = *balanceState3;

            //SENDING STARTED MESSAGE
            char started_message[75];
            sprintf(started_message, log_started_fmt, read_lamport_time(global), (int ) process_number, getpid(), getppid(), money_at_start[count-1]);

            Message message_start_send;
            MessageHeader messageHeader;
            messageHeader.s_magic = MESSAGE_MAGIC;
            messageHeader.s_payload_len = 75;
            messageHeader.s_local_time = read_lamport_time(global);
            messageHeader.s_type = STARTED;
            message_start_send.s_header = messageHeader;
            for (size_t i = 0; i < 75; i++) {
                message_start_send.s_payload[i] = started_message[i];
            }

            send_multicast((void*)babyMaybeProcess, &message_start_send);

            //READING STARTED MESSAGE
            for (size_t i = 1; i < number_of_child_procs+1; i++) {
                if (i != process_number) {

                    //increase before reading started message
                    incr_lamport_time(global);

                    Message message_start_receive;

//                    printf("receive started msg\n");
                    int receive_result = -1;
                    while (receive_result == -1) {
                        receive_result = receive((void*)babyMaybeProcess, i, &message_start_receive);
                    }


//                    printf("child %zu received message: %s\n", count, message_start_receive.s_payload);
                }
            }

            //log_received_all_started_fmt
            printf(log_received_all_started_fmt, read_lamport_time(global), (int ) process_number);
            fprintf(ev, log_received_all_started_fmt, read_lamport_time(global), (int ) process_number);

            Message any_message_receive;
            TransferOrder transferOrder;
            int done_procs, stop_signal, done = 0;
            while (done == 0) {
                while (receive_any((void*)babyMaybeProcess, &any_message_receive) == -1) {}

                incr_lamport_time(global);

                switch (any_message_receive.s_header.s_type) {
                    case TRANSFER:
                    {
                        printf("proc %zu case Transfer activated\n", process_number);
                        memcpy(&transferOrder, any_message_receive.s_payload, any_message_receive.s_header.s_payload_len);
                        if (process_number == transferOrder.s_src) {

                            if (process_number == 1) {
                                incr_lamport_time(global);
                                incr_lamport_time(global);
                            }

                            //increase before sending transfer message
                            incr_lamport_time(global);

                            printf("proc %zu is a source\n", process_number);
                            money_at_start[process_number-1] = money_at_start[process_number-1] - transferOrder.s_amount;

                            BalanceState * balanceState1 = new_balance(money_at_start[process_number-1],
                                                                       read_lamport_time(global), transferOrder.s_amount);
                            new_history->s_history_len = len;
                            new_history->s_history[read_lamport_time(global)] = *balanceState1;

                            size_t temp = len - read_lamport_time(global);

                            for (size_t i = 1; i < temp; i++) {
                                BalanceState * balanceState2 = new_balance(money_at_start[process_number-1],
                                                                           read_lamport_time(global)+i, transferOrder.s_amount);
                                new_history->s_history_len = len;
                                new_history->s_history[read_lamport_time(global)+i] = *balanceState2;
                            }

                            printf(log_transfer_out_fmt, read_lamport_time(global), (int ) process_number, transferOrder.s_amount, transferOrder.s_dst);
                            Message transfer_to_dst;
                            MessageHeader transferMessageHeader;
                            transferMessageHeader.s_magic = MESSAGE_MAGIC;
                            transferMessageHeader.s_payload_len = sizeof(transferOrder);
                            transferMessageHeader.s_local_time = read_lamport_time(global);
                            transferMessageHeader.s_type = TRANSFER;
                            transfer_to_dst.s_header = transferMessageHeader;

                            memcpy(transfer_to_dst.s_payload, &transferOrder, sizeof(transferOrder));

                            send(babyMaybeProcess, transferOrder.s_dst, &transfer_to_dst);
                            any_message_receive.s_header.s_type = -1;
                            break;
                        } else if (process_number == transferOrder.s_dst) {

                            printf("proc %zu is a destination\n", process_number);

                            incr_lamport_time(global);

                            if (read_lamport_time(global) <= any_message_receive.s_header.s_local_time) {
                                set_lamport_time(global, any_message_receive.s_header.s_local_time);
                                incr_lamport_time(global);
                            }

                            money_at_start[process_number-1] = money_at_start[process_number-1] + transferOrder.s_amount;

                            //отправить через balance state ноль в пендинг процесса отправителя через transferorder src

                            BalanceState * balanceState = new_balance(money_at_start[process_number-1],
                                                                      read_lamport_time(global), 0);
                            new_history->s_id = process_number;
                            new_history->s_history_len = len;
                            new_history->s_history[read_lamport_time(global)] = *balanceState;

                            size_t temp = len - read_lamport_time(global);

                            for (size_t i = 1; i < temp; i++) {
                                BalanceState * balanceState1 = new_balance(money_at_start[process_number-1],
                                                                           read_lamport_time(global)+i, 0);

                                new_history->s_id = process_number;
                                new_history->s_history_len = len;
                                new_history->s_history[read_lamport_time(global)+i] = *balanceState1;
                            }

                            incr_lamport_time(global);

                            printf("----------%zu Lamport time: %d\n", process_number, read_lamport_time(global));

                            Message send_ack_msg;
                            MessageHeader ackMessageHeader;
                            ackMessageHeader.s_type = ACK;
                            ackMessageHeader.s_payload_len = 0;
                            ackMessageHeader.s_local_time = read_lamport_time(global);
                            ackMessageHeader.s_magic = MESSAGE_MAGIC;
                            send_ack_msg.s_header = ackMessageHeader;

//                            printf("proc %zu dst will send msg to parent\n", process_number);
                            send(babyMaybeProcess, 0, &send_ack_msg);
//                            printf("proc dst sent msg to parent\n");
                            any_message_receive.s_header.s_type = -1;
                            break;
                        } else {
//                            printf("destination: %hhd\n", transferOrder.s_dst);
                            printf("proc %zu msg receive fault\n", process_number);
                        }
                    }
                    case STOP:
                    {
                        printf("proc %zu case STOP activated\n", process_number);
                        char message_done_char[40];

                        incr_lamport_time(global);

                        sprintf(message_done_char, log_done_fmt, read_lamport_time(global), (int )process_number, money_at_start[process_number-1]);

                        printf(log_done_fmt, read_lamport_time(global), (int )process_number, money_at_start[process_number-1]);

                        Message message_done_send;
                        MessageHeader doneMessageHeader;
                        doneMessageHeader.s_type = DONE;
                        doneMessageHeader.s_payload_len = 40;
                        doneMessageHeader.s_magic = MESSAGE_MAGIC;
                        doneMessageHeader.s_local_time = read_lamport_time(global);
                        message_done_send.s_header = doneMessageHeader;

                        for (size_t i = 0; i < 40; i++) {
                            message_done_send.s_payload[i] = message_done_char[i];
                        }

                        send_multicast(babyMaybeProcess, &message_done_send);

                        incr_lamport_time(global);

                        Message history_message_send;
                        MessageHeader historyMessageHeader;
                        historyMessageHeader.s_type = BALANCE_HISTORY;
                        historyMessageHeader.s_magic = MESSAGE_MAGIC;
                        historyMessageHeader.s_local_time = read_lamport_time(global);
                        historyMessageHeader.s_payload_len = sizeof(BalanceHistory);
                        history_message_send.s_header = historyMessageHeader;

                        memcpy(&history_message_send.s_payload, new_history, sizeof(BalanceHistory));
//                        printf("PAYLOAD: %d\n", new_history->s_history[1].s_balance);
                        int z = send((void*)babyMaybeProcess, 0, &history_message_send);
                        if (z == 0) {
//                            printf("HISTORY SENT\n");
                        }

                        any_message_receive.s_header.s_type = -1;

                        stop_signal = 1;
                        if (done_procs == global->number_of_child_procs - 2) {
                            //log all done
                            done = 1;
                            break;
                        }
                        break;
                    }
                    case DONE:
                    {
                        printf("proc %zu case DONE activated\n", process_number);
                        done_procs = done_procs + 1;
                        any_message_receive.s_header.s_type = -1;
//                        printf("proc %zu has %d proc done\n", process_number, done_procs);

                        if (stop_signal == 1 && done_procs == global->number_of_child_procs - 2) {
                            //log all done
                            done = 1;
                            break;
                        }
                        break;
                    }
                }
            }
            sleep(1);
            printf("proc %zu exited\n", process_number);
            exit(0);
        }
    }

    //CLOSING PIPES FOR PARENT PROC
    for (size_t i = 0; i < number_of_child_procs+1; i++) {
        for (size_t j = 0; j < number_of_child_procs+1; j++) {
            if (i != j && i != 0 && j != 0) {
                close(global->pipes_all[i][j].fd[0]);
                close(global->pipes_all[i][j].fd[1]);
                close(global->pipes_all[j][i].fd[0]);
                close(global->pipes_all[j][i].fd[1]);
//                printf("closed %zu %zu and %zu %zu pipes\n", i, j, j, i);
            }
        }
    }
    for (size_t i = 0; i < number_of_child_procs+1; i++) {
        if (i != process_number) {
            close(global->pipes_all[process_number][i].fd[0]);
            close(global->pipes_all[i][process_number].fd[1]);
        }
    }

    //READING STARTED MESSAGE BY PARENT
//    for (size_t i = 1; i < number_of_child_procs + 1; i++) {
//        Message message;
//        size_t header_read_result = -1;
//        while (header_read_result == -1) {
//            header_read_result = read(global->pipes_all[i][0].fd[0], &message.s_header, sizeof(MessageHeader));
//            read(global->pipes_all[i][0].fd[0], message.s_payload, message.s_header.s_payload_len);
//        }
//        printf("parent received: %s from proc #%zu\n", message.s_payload, i);
//    }

    baby_maybe_process * babyMaybeProcess = new_baby(process_number, global);

    for (size_t i = 1; i < number_of_child_procs+1; i++) {
        Message started_message_receive;
        size_t header_read_result = -1;
        while (header_read_result == -1) {
            header_read_result = receive((void *) babyMaybeProcess, i, &started_message_receive);
        }

        if (read_lamport_time(global) < started_message_receive.s_header.s_local_time) {
            set_lamport_time(global, started_message_receive.s_header.s_local_time);
            incr_lamport_time(global);
        } else {
            incr_lamport_time(global);
        }

        printf("parent received: %s from proc #%zu\n", started_message_receive.s_payload, i);
    }

    bank_robbery(babyMaybeProcess, (int )number_of_child_procs);
    printf("__________bank robbery end\n");

    incr_lamport_time(global);

    Message stop_message;
    MessageHeader messageHeader;
    messageHeader.s_type = STOP;
    messageHeader.s_payload_len = 0;
    messageHeader.s_local_time = read_lamport_time(global);
    messageHeader.s_magic = MESSAGE_MAGIC;
    stop_message.s_header = messageHeader;
    printf("STOP MESSAGE SENT\n");

    send_multicast(babyMaybeProcess, &stop_message);

    for (size_t count = 1; count < number_of_child_procs + 1; count++) wait(NULL);

    for (size_t i = 1; i < number_of_child_procs + 1; i++) {
        Message message;
        read(global->pipes_all[i][0].fd[0], &message.s_header, sizeof(MessageHeader));
        read(global->pipes_all[i][0].fd[0], message.s_payload, message.s_header.s_payload_len);
//        printf("parent received: %s from proc #%zu\n", message.s_payload, i);
        if (read_lamport_time(global) < message.s_header.s_local_time) {
            set_lamport_time(global, message.s_header.s_local_time);
            incr_lamport_time(global);
        } else {
            incr_lamport_time(global);
        }
    }

    printf("parent lamport time after work done: %d\n", read_lamport_time(global));

    Message historyMessageReceive[number_of_child_procs+1];
    for (size_t i = 1; i < number_of_child_procs+1; i++) {
        while (1) {
            read(global->pipes_all[i][0].fd[0], &historyMessageReceive[i].s_header, sizeof(MessageHeader));
            if (historyMessageReceive[i].s_header.s_type == BALANCE_HISTORY) {
                read(global->pipes_all[i][0].fd[0], historyMessageReceive[i].s_payload, historyMessageReceive[i].s_header.s_payload_len);

                if (read_lamport_time(global) < historyMessageReceive[i].s_header.s_local_time) {
                    set_lamport_time(global, historyMessageReceive[i].s_header.s_local_time);
                    incr_lamport_time(global);
                } else {
                    incr_lamport_time(global);
                }

                break;
            }
        }
    }

    AllHistory allHistory;
    allHistory.s_history_len = number_of_child_procs; ///< should be equal to the number of children
    for (size_t i = 0; i < number_of_child_procs; i++) {
        BalanceHistory balanceHistory;
        memcpy(&balanceHistory, &historyMessageReceive[i+1].s_payload, sizeof(BalanceHistory));

//        for (size_t j = 0; j < 16; j++) {
//            if (balanceHistory.s_history[j].s_balance > balanceHistory.s_history[j-1].s_balance) {
//                allHistory.s_history[i-1].s_history[j].s_balance_pending_in = 0;
//            }
//        }

        allHistory.s_history[i] = balanceHistory;
    }

    for (size_t i = 1; i < number_of_child_procs+1; i++) {
        for (size_t time = 0; time < len; time++) {
            if (allHistory.s_history[i].s_history[time].s_balance < allHistory.s_history[i].s_history[time+1].s_balance) {

                allHistory.s_history[i-1].s_history[time+1].s_balance_pending_in = 0;

                size_t temp = len - time+1;
                for (size_t j = 0; j < temp; j++) {
                    allHistory.s_history[i-1].s_history[time+j+1].s_balance_pending_in = 0;
                }
            }
        }
    }

    for (size_t time = 0; time < len; time++) {
        if (allHistory.s_history[0].s_history[time].s_balance < allHistory.s_history[0].s_history[time+1].s_balance) {
            allHistory.s_history[4].s_history[time+1].s_balance_pending_in = 0;

            size_t temp = len - time+1;
            for (size_t j = 0; j < temp; j++) {
                allHistory.s_history[4].s_history[time+j+1].s_balance_pending_in = 0;
            }
        }
    }

    print_history(&allHistory);

    fclose(ev);
    fclose(pi);

    return 0;
}
