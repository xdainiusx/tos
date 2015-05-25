
#include <kernel.h>

PORT_DEF port[MAX_PORTS];
PORT next_free_port;



PORT create_port()
{
	return create_new_port(active_proc);
}


PORT create_new_port (PROCESS owner)
{
	PORT port;
	volatile int flag;

	DISABLE_INTR(flag);

	assert(owner->magic == MAGIC_PCB);

	if(next_free_port == NULL) {
		panic("create_new_port(): PORT array is full");
	}
	port = next_free_port;
	next_free_port = port->next;
	port->used              = TRUE;
	port->magic             = MAGIC_PORT;
	port->owner             = owner;
	port->blocked_list_head = NULL;
	port->blocked_list_tail = NULL;
	port->open              = TRUE;

	if(owner->first_port == NULL) {
		port->next = NULL;
	}
	else {
		port->next = owner->first_port;
	}
	owner->first_port = port;

	ENABLE_INTR(flag);
	return port;
}


void open_port (PORT port)
{
	assert(port->magic == MAGIC_PORT);
	port->open = TRUE;
}


void close_port (PORT port)
{
	assert(port->magic == MAGIC_PORT);
	port->open = FALSE;
}


void add_to_send_blocked_list(PORT port, PROCESS proc)
{
	volatile int flag;

	DISABLE_INTR(flag);

	assert(port->magic == MAGIC_PORT);
	assert(proc->magic == MAGIC_PCB);
	if(port->blocked_list_head == NULL){
		port->blocked_list_head = proc;
	}
	else {
		port->blocked_list_tail->next_blocked = proc;
	}
	port->blocked_list_tail = proc;
	proc->next_blocked = NULL;

	ENABLE_INTR(flag);
}


void send (PORT dest_port, void* data)
{
	PROCESS dest_process;
	volatile int flag;

	DISABLE_INTR(flag);

	assert(dest_port->magic == MAGIC_PORT);
	dest_process = dest_port->owner;
	assert(dest_process->magic == MAGIC_PCB);

	if(dest_port->open && dest_process->state == STATE_RECEIVE_BLOCKED){
		// Receiver is receive blocked.
		// We can deliver our message immediately.
		dest_process->param_proc = active_proc;
		dest_process->param_data = data;
		active_proc->state       = STATE_REPLY_BLOCKED;  // message doesn't have this
		add_ready_queue(dest_process);
	}
	else {
		// Receiver is busy or the port is closed.
		// Get on the send blocked queue of the port.
		add_to_send_blocked_list(dest_port, active_proc);
		active_proc->state = STATE_SEND_BLOCKED;
		active_proc->param_data = data;
	}
	active_proc->param_data = data;
	remove_ready_queue(active_proc);
	resign();

	ENABLE_INTR(flag);
}


void message (PORT dest_port, void* data)
{
	PROCESS dest_process;
	volatile int flag;

	DISABLE_INTR(flag);

	assert(dest_port->magic == MAGIC_PORT);
	dest_process = dest_port->owner;
	assert(dest_process->magic == MAGIC_PCB);

	if(dest_port->open && dest_process->state == STATE_RECEIVE_BLOCKED) {
		dest_process->param_proc = active_proc;
		dest_process->param_data = data;
		add_ready_queue(dest_process);
	}
	else {
		add_to_send_blocked_list(dest_port, active_proc);
		remove_ready_queue(active_proc);
		active_proc->state = STATE_MESSAGE_BLOCKED;
		active_proc->param_data = data;
	}
	resign();

	ENABLE_INTR(flag);
}



void* receive (PROCESS* sender)
{
	PROCESS deliver_proc;
	PORT port;
	void *data;
	volatile int flag;

	DISABLE_INTR(flag);

	data = NULL;
	port = active_proc->first_port;
	if(port == NULL){
		panic("receive(): no port created for this process");
	}

	while(port != NULL){
		assert(port->magic == MAGIC_PORT);
		if(port->open && port->blocked_list_head != NULL){
			break;
		}
		port = port->next;
	}

	if(port != NULL) {
		deliver_proc = port->blocked_list_head;
		assert(deliver_proc->magic == MAGIC_PCB);
		*sender = deliver_proc;
		data = deliver_proc->param_data;
		port->blocked_list_head = port->blocked_list_head->next_blocked;

		if(port->blocked_list_head == NULL){
			port->blocked_list_tail = NULL;
		}

		if(deliver_proc->state == STATE_MESSAGE_BLOCKED){
			add_ready_queue(deliver_proc);

			ENABLE_INTR(flag);
			return data;
		}
		else if(deliver_proc->state == STATE_SEND_BLOCKED) {
			deliver_proc->state = STATE_REPLY_BLOCKED;

			ENABLE_INTR(flag);
			return data;
		}

	}

	/* No messages pending */
	remove_ready_queue(active_proc);
	active_proc->param_data = data;
	active_proc->state = STATE_RECEIVE_BLOCKED;
	resign();
	*sender = active_proc->param_proc;
	data = active_proc->param_data;

	ENABLE_INTR(flag);
	return data;
}


void reply (PROCESS sender)
{
	volatile int flag;

	DISABLE_INTR(flag);

	if(sender->state != STATE_REPLY_BLOCKED){
		panic("reply(): reply not blocked");
	}
	add_ready_queue(sender);
	resign();

	ENABLE_INTR(flag);
}


void init_ipc()
{
	int i;

	next_free_port = port;

	for(i=0; i < MAX_PORTS - 1; i++){
		port[i].used = FALSE;
		port[i].magic = MAGIC_PORT;
		port[i].next = &port[i+1];
	}
	port[MAX_PORTS - 1].used = FALSE;
	port[MAX_PORTS - 1].magic = MAGIC_PORT;
	port[MAX_PORTS - 1].next = NULL;
}
