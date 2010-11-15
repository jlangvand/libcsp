/*
Cubesat Space Protocol - A small network-layer protocol designed for Cubesats
Copyright (C) 2010 GomSpace ApS (gomspace.com)
Copyright (C) 2010 AAUSAT3 Project (aausat3.space.aau.dk)

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

/* SocketCAN driver */

#include <stdint.h>
#include <stdio.h>

#include <csp/interfaces/can.h>

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <errno.h>

#include <pthread.h>
#include <semaphore.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <sys/queue.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <net/if.h>

#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/socket.h>
#include <bits/socket.h>

/* Number of mailboxes */
#define MBOX_NUM 5

/* These constants are not defined for Blackfin */
#if !defined(PF_CAN) && !defined(AF_CAN)
    #define PF_CAN 29
    #define AF_CAN PF_CAN
#endif

int can_socket; /** SocketCAN socket handle */
sem_t mbox_sem;	/** Mailbox pool semaphore */

/** Callback functions */
can_tx_callback_t txcb;
can_rx_callback_t rxcb;

/* Thread pool */
typedef enum {
    MBOX_FREE = 0,
    MBOX_USED = 1,
} mbox_state_t;

typedef struct {
	int id;					/** Mailbox id */
    pthread_t thread;  		/** Thread handle */
    sem_t signal_sem;   	/** Signalling semaphore */
    mbox_state_t state;		/** Thread state */
    uint32_t cfpid;     	/** CFP identifier */
    struct can_frame frame;	/** CAN Frame */
} mbox_t;

/* List of mailboxes */
static mbox_t mbox[MBOX_NUM];

/* Mailbox thread */
static void * mbox_tx_thread(void * parameters) {

	/* Set thread parameters */
    mbox_t * m = (mbox_t *)parameters;

    while (1) {

        /* Wait for a new packet to process */
        sem_wait(&(m->signal_sem));

		/* Send frame */
		uint8_t tries = 0;
		while (write(can_socket, &m->frame, sizeof(m->frame)) != sizeof(m->frame)) {
			if (++tries < 10 && errno == ENOBUFS) {
				/* Wait 1 ms and try again */
				usleep(1000);
			} else {
				perror("write");
				break;
			}
		}

		/* Call tx callback */
		if (txcb) txcb(m->id);

    }

    /* We should never reach this point */
    pthread_exit(NULL);

}

static void * mbox_rx_thread(void * parameters) {

    struct can_frame frame;
    int nbytes;

    while (1) {
        /* Read CAN frame */
        nbytes = read(can_socket, &frame, sizeof(frame));

        if (nbytes != sizeof(frame)) {
            printf("Read incomplete CAN frame\n");
            continue;
        }

        /* Frame type */
        if (frame.can_id & (CAN_ERR_FLAG | CAN_RTR_FLAG) || !(frame.can_id & CAN_EFF_FLAG)) {
        	/* Drop error and remote frames */
        	printf("Discarding ERR/RTR/SFF frame\r\n");
        } else {
        	/* Strip flags */
        	frame.can_id &= CAN_EFF_MASK;
        }


        /* Call RX callback */
        if (rxcb) rxcb((can_frame_t *)&frame);
    }

    /* We should never reach this point */
    pthread_exit(NULL);

}

int can_mbox_init(void) {

    int i;
    mbox_t * m;

    for (i = 0; i < MBOX_NUM; i++) {
    	m = &mbox[i];
        m->state = MBOX_FREE;
        m->cfpid = 0;

        /* Init signal semaphore */
        if (sem_init(&(m->signal_sem), 0, 1) != 0) {
            perror("sem_init");
            return -1;
        } else {
            sem_wait(&(m->signal_sem));
        }

        /* Create mailbox */
        if (pthread_create(&m->thread, NULL, mbox_tx_thread, (void *)m) != 0) {
            perror("pthread_create");
            return -1;
        }
    }

    /* Init mailbox pool semaphore */
    sem_init(&mbox_sem, 0, 1);

    return 0;

}

int can_mbox_get(void) {

    int i;
    mbox_t * m;

    sem_wait(&mbox_sem);
    for (i = 0; i < MBOX_NUM; i++) {
        m = &mbox[i];

        if(m->state == MBOX_FREE) {
            m->state = MBOX_USED;
            sem_post(&mbox_sem);
            return i;
        }
    }
    sem_post(&mbox_sem);

    /* No free thread */
    return -1;
}

int can_mbox_release(int m) {
	mbox[m].cfpid = 0;
    sem_wait(&mbox_sem);
    mbox[m].state = MBOX_FREE;
    sem_post(&mbox_sem);
    return 0;
}

int can_mbox_data(int m, can_id_t id, uint8_t data[], uint8_t dlc) {

	if (dlc > 8) {
		return -1;
	} else {
		int i;

		/* Copy identifier */
		mbox[m].frame.can_id = id | CAN_EFF_FLAG;

		/* Copy data to frame */
		for (i = 0; i < dlc; i++)
			mbox[m].frame.data[i] = data[i];

		/* Set DLC */
		mbox[m].frame.can_dlc = dlc;

		return 0;
	}

}

int can_mbox_send(int m) {

	/* Signal thread to start */
	sem_post(&(mbox[m].signal_sem));

	return 0;

}

int can_init(char * ifc, uint32_t id, uint32_t mask, can_tx_callback_t atxcb, can_rx_callback_t arxcb) {

	struct ifreq ifr;
	struct sockaddr_can addr;
	pthread_t rx_thread;

	txcb = atxcb;
	rxcb = arxcb;

	/* Create socket */
	if ((can_socket = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
		perror("socket");
		return -1;
	}

	/* Locate interface */
	strncpy(ifr.ifr_name, ifc, IFNAMSIZ - 1);
	if (ioctl(can_socket, SIOCGIFINDEX, &ifr) < 0) {
		perror("ioctl");
		return -1;
	}

	/* Bind the socket to CAN interface */
	addr.can_family = AF_CAN;
	addr.can_ifindex = ifr.ifr_ifindex;
	if (bind(can_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		return -1;
	}

	/* Set promiscuous mode */
	if (mask) {
		struct can_filter filter;
		filter.can_id   = id;
		filter.can_mask = mask;
		if (setsockopt(can_socket, SOL_CAN_RAW, CAN_RAW_FILTER, &filter, sizeof(filter)) < 0) {
			perror("setsockopt");
			return -1;
		}
	}

	/* Create receive thread */
	if (pthread_create(&rx_thread, NULL, mbox_rx_thread, NULL) != 0) {
		perror("pthread_create");
		return -1;
	}

	/* Create TX thread pool */
	if (can_mbox_init() != 0) {
		printf("Failed to create tx thread pool\n");
		return -1;
	}

	return 0;

}