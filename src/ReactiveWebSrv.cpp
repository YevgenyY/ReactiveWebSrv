
#include "ace/Log_Msg.h"
#include "ace/INET_Addr.h"
#include "ace/SOCK_Acceptor.h"
#include "ace/Reactor.h"
#include "ace/Acceptor.h"

#include "ace/Message_Block.h"
#include "ace/SOCK_Stream.h"
#include "ace/Svc_Handler.h"

class Echo_Svc_Handler : public ACE_Svc_Handler<ACE_SOCK_STREAM, ACE_NULL_SYNCH>
{
  typedef ACE_Svc_Handler<ACE_SOCK_STREAM, ACE_NULL_SYNCH> super;

public:
  int open (void * = 0);

protected:
  // Called when input is available from the client.
  virtual int handle_input (ACE_HANDLE fd = ACE_INVALID_HANDLE);

  // Called when output is possible.
  virtual int handle_output (ACE_HANDLE fd = ACE_INVALID_HANDLE);

  // Called when this handler is removed from the ACE_Reactor.
  virtual int handle_close (ACE_HANDLE handle,
                            ACE_Reactor_Mask close_mask);
};

int Echo_Svc_Handler::open (void *p)
{
  if (super::open (p) == -1)
    return -1;

  ACE_TCHAR peer_name[MAXHOSTNAMELEN];
  ACE_INET_Addr peer_addr;
  if (this->peer ().get_remote_addr (peer_addr) == 0 &&
      peer_addr.addr_to_string (peer_name, MAXHOSTNAMELEN) == 0)
    ACE_DEBUG ((LM_DEBUG,
                ACE_TEXT ("(%P|%t) Connection from %s\n"),
                peer_name));
  return 0;
}

int
Echo_Svc_Handler::handle_input (ACE_HANDLE)
{
  const size_t INPUT_SIZE = 4096;
  char buffer[INPUT_SIZE];
  ssize_t recv_cnt, send_cnt;

  recv_cnt = this->peer ().recv (buffer, sizeof(buffer));
  if (recv_cnt <= 0)
    {
      ACE_DEBUG ((LM_DEBUG,
    		  ACE_TEXT ("(%P|%t) Connection closed\n")));
      return -1;
    }

  send_cnt =
    this->peer ().send (buffer, (size_t) recv_cnt);
  if (send_cnt == recv_cnt)
    return 0;
  if (send_cnt == -1 && ACE_OS::last_error () != EWOULDBLOCK)
    ACE_ERROR_RETURN ((LM_ERROR,
                       ACE_TEXT ("(%P|%t) %p\n"),
                       ACE_TEXT ("send")),
                      0);
  if (send_cnt == -1)
    send_cnt = 0;
  ACE_Message_Block *mb;
  size_t remaining = (size_t)(recv_cnt - send_cnt);
  ACE_NEW_RETURN (mb, ACE_Message_Block (&buffer[send_cnt], remaining), -1);
  int output_off = this->msg_queue ()->is_empty ();
  ACE_Time_Value nowait (ACE_OS::gettimeofday ());
  if (this->putq (mb, &nowait) == -1)
    {
      ACE_ERROR ((LM_ERROR,
                  ACE_TEXT ("(%P|%t) %p; discarding data\n"),
                  ACE_TEXT ("enqueue failed")));
      mb->release ();
      return 0;
    }

  if (output_off)
    return this->reactor ()->register_handler
      (this, ACE_Event_Handler::WRITE_MASK);

  return 0;
}

int
Echo_Svc_Handler::handle_output (ACE_HANDLE)
{
  ACE_Message_Block *mb;
  ACE_Time_Value nowait (ACE_OS::gettimeofday ());
  while (-1 != this->getq (mb, &nowait))
    {
      ssize_t send_cnt =
        this->peer ().send (mb->rd_ptr (), mb->length ());
      if (send_cnt == -1)
        ACE_ERROR ((LM_ERROR,
                    ACE_TEXT ("(%P|%t) %p\n"),
                    ACE_TEXT ("send")));
      else
        mb->rd_ptr ((size_t)send_cnt);
      if (mb->length () > 0)
        {
          this->ungetq (mb);
          break;
        }
      mb->release ();
    }
  return (this->msg_queue ()->is_empty ()) ? -1 : 0;
}

int
Echo_Svc_Handler::handle_close (ACE_HANDLE h, ACE_Reactor_Mask mask)
{
  if (mask == ACE_Event_Handler::WRITE_MASK)
    return 0;
  return super::handle_close (h, mask);
}


typedef ACE_Acceptor<Echo_Svc_Handler, ACE_SOCK_ACCEPTOR> Echo_Acceptor;

int ACE_TMAIN (int, ACE_TCHAR *[])
{
  ACE_INET_Addr port_to_listen ("HAStatus");
  Echo_Acceptor acceptor;
  if (acceptor.open (port_to_listen) == -1)
    return 1;

  ACE_Reactor::instance ()->run_reactor_event_loop ();

  return (0);
}
