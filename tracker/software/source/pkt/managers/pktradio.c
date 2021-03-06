/*
    Aerospace Decoder - Copyright (C) 2018 Bob Anderson (VK2GJ)

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*/

/**
 * @file        pktradio.c
 * @brief       Radio manager.
 *
 * @addtogroup  managers
 * @{
 */

#include "pktconf.h"
#ifndef PKT_IS_TEST_PROJECT
#include "radio.h"
#endif
#include "si446x.h"
#include "debug.h"
#include "geofence.h"

/**
 * @brief   Process radio task requests.
 * @notes   Task objects posted to the queue are processed per radio.
 * @notes   The queue is blocked while the radio driver functions execute.
 * @notes   Receive tasks start the receive/decode system which are threads.
 * @notes   Transmit tasks should be handled in threads (and are in 446x).
 *
 * @param[in] arg pointer to a @p radio task queue for this thread.
 *
 * @return  status (MSG_OK) on exit.
 *
 * @notapi
 */
THD_FUNCTION(pktRadioManager, arg) {
  /* When no task in queue use this rate. */
#define PKT_RADIO_TASK_MANAGER_IDLE_RATE_MS     100

  /* When a TX task is submitted to radio switch to this rate. */
#define PKT_RADIO_TASK_MANAGER_TX_RATE_MS       100

/* Continue at TX rate for this number of cycles. */
  /* TODO: Deprecate this gear shift stuff. */
#define PKT_RADIO_TASK_MANAGER_TX_HYSTERESIS    10

  packet_svc_t *handler = arg;

  dyn_objects_fifo_t *the_radio_fifo = handler->the_radio_fifo;

  chDbgCheck(arg != NULL);

  sysinterval_t poll_rate = PKT_RADIO_TASK_MANAGER_IDLE_RATE_MS;
  /* TODO: Deprecate this gear shift stuff. */
  uint8_t poll_hysteresis = 0;

  objects_fifo_t *radio_queue = chFactoryGetObjectsFIFO(the_radio_fifo);

  chDbgAssert(radio_queue != NULL, "no queue in radio manager FIFO");

  /* Run until terminate request and no outstanding TX tasks. */
  while(!(chThdShouldTerminateX() && handler->tx_count == 0)) {
    /* Check for task requests. */
    radio_task_object_t *task_object;
    msg_t fifo_msg = chFifoReceiveObjectTimeout(radio_queue,
                         (void *)&task_object,
                         TIME_MS2I(poll_rate));
    if(fifo_msg == MSG_TIMEOUT) {
      /* TODO: Deprecate this gear shift stuff. */
      if(poll_hysteresis == 0)
        poll_rate = PKT_RADIO_TASK_MANAGER_IDLE_RATE_MS;
      else
        --poll_hysteresis;
      continue;
    }
    /* Something to do. */

    radio_unit_t radio = handler->radio;
    /* Process command. */
    switch(task_object->command) {
    case PKT_RADIO_RX_OPEN: {

       /* Create the packet management services. */
      if(pktIncomingBufferPoolCreate(radio) == NULL) {
        pktAddEventFlags(handler, (EVT_PKT_BUFFER_MGR_FAIL));
        break;
      }
      /* Create callback manager. */
      if(pktCallbackManagerCreate(radio) == NULL) {
        pktAddEventFlags(handler, (EVT_PKT_CBK_MGR_FAIL));
        pktIncomingBufferPoolRelease(handler);
        break;
      }
      /* Switch on modulation type. */
      switch(task_object->type) {
        case MOD_AFSK: {
          /* Create the AFSK decoder (includes PWM, filters, etc.). */
          AFSKDemodDriver *driver = pktCreateAFSKDecoder(handler);
          handler->link_controller = driver;
          /* If AFSK start failed send event but leave managers running. */
          if(driver == NULL) {
            pktAddEventFlags(handler, (EVT_AFSK_START_FAIL));
/*            pktBufferManagerRelease(handler);
            pktCallbackManagerRelease(handler);*/
            break;
          }
          break;
        } /* End case PKT_RADIO_OPEN. */

        case MOD_NONE:
        case MOD_2FSK: {
          break;
        }
        break;
      } /* End switch on modulation type. */

      /* Initialise the radio. */
      Si446x_conditional_init(radio);
      break;
    } /* End case PKT_RADIO_OPEN. */


    case PKT_RADIO_RX_START: {
      /* The function switches on mod type so no need for switch here. */
      switch(task_object->type) {
      case MOD_AFSK: {
        pktAcquireRadio(radio, TIME_INFINITE);

        /* TODO: Move these 446x calls into abstracted LLD. */
        Si446x_setBandParameters(radio,
                                 task_object->base_frequency,
                                 task_object->step_hz);

        Si446x_receiveNoLock(radio,
                             task_object->base_frequency,
                             task_object->step_hz,
                             task_object->channel,
                             task_object->squelch,
                             MOD_AFSK);
        /* TODO: If decoder is not running error out. */

        pktStartDecoder(radio);
        //handler->rx_active = true;
        /* Allow transmit requests. */
        pktReleaseRadio(radio);
        break;
        } /* End case MOD_AFSK. */

      case MOD_NONE:
      case MOD_2FSK: {
        break;
        }
      } /* End switch on task_object->type. */
      break;
    } /* End case PKT_RADIO_RX. */

    case PKT_RADIO_RX_STOP: {
      switch(task_object->type) {
            case MOD_AFSK: {
              pktAcquireRadio(radio, TIME_INFINITE);
              pktStopDecoder(handler->radio);
              //handler->rx_active = false;
              pktReleaseRadio(radio);
              break;
              } /* End case. */

            case MOD_NONE:
            case MOD_2FSK: {
              break;
              }
       } /* End switch. */
      break;
    } /* End case PKT_RADIO_RX_STOP. */

    case PKT_RADIO_TX_SEND: {
      /*
       * TODO: Currently the decoder is not paused.
       * Is it necessary since the RX is not outputting data during TX?
       */

      /* Give each send a sequence number. */
      ++handler->radio_tx_config.tx_seq_num;
      pktPauseReception(radio);
      if(pktLLDsendPacket(task_object)) {
        /*
         * Keep count of active sends.
         * Shutdown or resume receive when all done.
         */
        handler->tx_count++;

        /* TODO: Deprecate this gear shift stuff. */
        poll_hysteresis = PKT_RADIO_TASK_MANAGER_TX_HYSTERESIS;
        poll_rate = PKT_RADIO_TASK_MANAGER_TX_RATE_MS;
        /* Send Successfully enqueued.
         * Unlike receive the task object is held by the TX until complete.
         * This is non blocking as radio transmit runs in a thread.
         * The radio task object is released in the TX thread release task.
         */
        continue;
      }
      /* Send failed so release send packet object(s) and task object. */
      packet_t pp = task_object->packet_out;
      pktReleaseBufferChain(pp);
      pktResumeReception(radio);
      break;
    } /* End case PKT_RADIO_TX. */

    case PKT_RADIO_RX_CLOSE: {
      event_listener_t el;
      event_source_t *esp;
      thread_t *decoder = NULL;
      switch(task_object->type) {
      case MOD_AFSK: {
        /* TODO: Implement LLD function for this. */
        Si446x_disableReceive(radio);
        /* TODO: This should be a function back in pktservice or pktradio. */
        esp = pktGetEventSource((AFSKDemodDriver *)handler->link_controller);
        pktRegisterEventListener(esp, &el, USR_COMMAND_ACK, DEC_CLOSE_EXEC);
        decoder = ((AFSKDemodDriver *)(handler->link_controller))->decoder_thd;

        /* TODO: Check that decoder will release in WAIT state.
         * Send event to release AFSK resources and terminate thread.
         */
        chEvtSignal(decoder, DEC_COMMAND_CLOSE);

        /* Then release common services and thread heap. */
        break;
        }

      case MOD_NONE:
      case MOD_2FSK: {
        break;
        } /* End case DECODE_FSK. */
      } /* End switch on link_type. */
      if(decoder == NULL)
        /* No decoder processed. */
        break;

      /* Wait for the decoder to stop. */
      eventflags_t evt;
      do {
        chEvtWaitAny(USR_COMMAND_ACK);

        /* Wait for correct event at source.
         */
        evt = chEvtGetAndClearFlags(&el);
      } while (evt != DEC_CLOSE_EXEC);
      pktUnregisterEventListener(esp, &el);

      /*
       *  Release decoder thread heap when it terminates.
       */
      chThdWait(decoder);

      /* Release packet services. */
      pktIncomingBufferPoolRelease(handler);
      pktCallbackManagerRelease(handler);

      /*
       * Signal close completed for this session.
       * Any new open that is queued on the sempahore will be readied.
       */
      chBSemSignal(&handler->close_sem);
      break;
      } /*end case close. */

    case PKT_RADIO_TX_THREAD: {
      /* Get thread exit code a free memory. */
      msg_t send_msg = chThdWait(task_object->thread);

      bool rxok = true;
      /* If no transmissions pending then enable RX or shutdown. */
      if(--handler->tx_count == 0) {
        if(pktIsReceivePaused(radio)) {
          rxok = pktLLDresumeReceive(radio);
          pktResumeReception(radio);
        } else {
          Si446x_shutdown(radio);
        }
      }

      if(send_msg != MSG_OK) {
        if(send_msg == MSG_TIMEOUT) {
          TRACE_ERROR("SI   > Transmit timeout");
        }
        if(send_msg == MSG_RESET) {
          TRACE_ERROR("SI   > Transmit failed to start");
        }
      }
      if(!rxok) {
        TRACE_ERROR("SI   > Receive failed to resume after transmit");
      }
      break;
    } /* End case PKT_RADIO_TX_THREAD */

    } /* End switch on command. */
    /* Perform radio task callback if specified. */
    if(task_object->callback != NULL)
      /*
       * Perform the callback.
       * The callback should be brief and non-blocking.
       */
      task_object->callback(task_object);
    /* Return radio task object to free list. */
    chFifoReturnObject(radio_queue, (radio_task_object_t *)task_object);
  } /* End while should terminate(). */
  chThdExit(MSG_OK);
}


thread_t *pktRadioManagerCreate(radio_unit_t radio) {

  packet_svc_t *handler = pktGetServiceObject(radio);

  chDbgAssert(handler != NULL, "invalid radio ID");

  /* Create the radio manager name. */
  chsnprintf(handler->rtask_name, sizeof(handler->rtask_name),
             "%s%02i", PKT_RADIO_TASK_QUEUE_PREFIX, radio);

  dyn_objects_fifo_t *the_radio_fifo =
      chFactoryCreateObjectsFIFO(handler->rtask_name,
      sizeof(radio_task_object_t),
      RADIO_TASK_QUEUE_MAX, sizeof(msg_t));

  chDbgAssert(the_radio_fifo != NULL, "unable to create radio task queue");

  if(the_radio_fifo == NULL)
    return NULL;

  handler->the_radio_fifo = the_radio_fifo;

  dbgPrintf(DBG_INFO, "PKT  > radio manager thread created. FIFO @ 0x%x\r\n",
            the_radio_fifo);

  /* Start the task dispatcher thread. */
  handler->radio_manager = chThdCreateFromHeap(NULL,
              THD_WORKING_AREA_SIZE(PKT_RADIO_MANAGER_WA_SIZE),
              handler->rtask_name,
              NORMALPRIO - 10,
              pktRadioManager,
              handler);

  chDbgAssert(handler->radio_manager != NULL,
              "unable to create radio task thread");

  if(handler->radio_manager == NULL) {
    chFactoryReleaseObjectsFIFO(the_radio_fifo);
    return NULL;
  }
  return handler->radio_manager;
}

/**
 * TODO: This needs review. Is it robust enough?
 */
void pktRadioManagerRelease(radio_unit_t radio) {
  packet_svc_t *handler = pktGetServiceObject(radio);
  chThdTerminate(handler->radio_manager);
  chThdWait(handler->radio_manager);
  chFactoryReleaseObjectsFIFO(handler->the_radio_fifo);
}

/**
 * @brief   Get a radio command task object.
 * @post    A task object is returned ready for filling and submission.
 *
 * @param[in]   radio   radio unit ID.
 * @param[in]   timeout maximum time to wait for a task to be submitted.
 * @param[in]   rt      pointer to a task object pointer.
 *
 * @return  Status of the operation.
 * @retval  MSG_TIMEOUT an object could not be obtained within the timeout.
 * @retval  MSG_OK      an object has been fetched.
 *
 * @api
 */
msg_t pktGetRadioTaskObject(radio_unit_t radio,
                            sysinterval_t timeout,
                            radio_task_object_t **rt) {

  packet_svc_t *handler = pktGetServiceObject(radio);

  chDbgAssert(handler != NULL, "invalid radio ID");

  dyn_objects_fifo_t *task_fifo =
      chFactoryFindObjectsFIFO(handler->rtask_name);
  chDbgAssert(task_fifo != NULL, "unable to find radio task fifo");

  objects_fifo_t *task_queue = chFactoryGetObjectsFIFO(task_fifo);
  chDbgAssert(task_queue != NULL, "no objects fifo list");

  *rt = chFifoTakeObjectTimeout(task_queue, TIME_MS2I(timeout));

  if(*rt == NULL) {
    /* Timeout waiting for object. */
    /* Release find reference to the FIFO (decrease reference count). */
    chFactoryReleaseObjectsFIFO(task_fifo);
    return MSG_TIMEOUT;
  }
  (*rt)->handler = handler;
  return MSG_OK;
}

/**
 * @brief   Submit a radio command to the task manager.
 * @post    A task object is created and submitted to the radio manager.
 *
 * @param[in]   radio   radio unit ID.
 * @param[in]   object  radio task object to be submitted.
 * @param[in]   cb      function to call with result (can be NULL).
 *
 * @api
 */
void pktSubmitRadioTask(radio_unit_t radio,
                         radio_task_object_t *object,
                         radio_task_cb_t cb) {

  packet_svc_t *handler = pktGetServiceObject(radio);
  chDbgAssert(handler != NULL, "invalid radio ID");

  dyn_objects_fifo_t *task_fifo = handler->the_radio_fifo;
  chDbgAssert(task_fifo != NULL, "no radio task fifo");

  objects_fifo_t *task_queue = chFactoryGetObjectsFIFO(task_fifo);
  chDbgAssert(task_queue != NULL, "no objects fifo list");

  /* Populate the object with information from request. */

  object->handler = handler;
  object->callback = cb;

  /*
   * Submit the task to the queue.
   * The task thread will process the request.
   * The task object is returned to the free list.
   * If a callback is specified it is called before the task object is freed.
   */
  chFifoSendObject(task_queue, object);
}

/**
 * @brief   Called by transmit threads to schedule release after completing.
 * @post    A thread release task is posted to the radio manager queue.
 *
 * @param[in]   radio   radio unit ID.
 * @param[in]   thread  thread reference.
 *
 * @api
 */
void pktScheduleSendComplete(radio_task_object_t *rto,
                              thread_t *thread) {

  packet_svc_t *handler = rto->handler;

  radio_unit_t radio = handler->radio;
  /* The handler and radio ID are set in returned object. */
  rto->command = PKT_RADIO_TX_THREAD;
  rto->thread = thread;
  /* Submit guaranteed to succeed by design. */
  pktSubmitRadioTask(radio, rto, rto->callback);
}

/**
 * @brief   Acquire exclusive access to radio.
 * @notes   returns when radio unit acquired.
 *
 * @param[in] radio     radio unit ID.
 * @param[in] timeout   time to wait for acquisition.
 *
 * @return              A message specifying the result.
 * @retval MSG_OK       if the radio has been successfully acquired.
 * @retval MSG_TIMEOUT  if the radio could not be acquired within specified time.
 * @retval MSG_RESET    if the radio can not be used due to a system abort.
 *
 * @api
 */
msg_t pktAcquireRadio(const radio_unit_t radio,
                      const sysinterval_t timeout) {
  packet_svc_t *handler = pktGetServiceObject(radio);
  return chBSemWaitTimeout(&handler->radio_sem, timeout);
}

/**
 * @brief   Release exclusive access to radio.
 * @notes   returns when radio unit is released.
 *
 * @param[in] radio    radio unit ID.
 *
 * @api
 */
void pktReleaseRadio(const radio_unit_t radio) {
  packet_svc_t *handler = pktGetServiceObject(radio);
  chBSemSignal(&handler->radio_sem);
}

/**
 * @brief   Compute an operating frequency.
 * @notes   All special frequency parameters are handled.
 *
 * @param[in] base_freq    Radio base frequency.
 * @param[in] step         Radio channel step frequency.
 * @param[in] chan         Radio channel number.
 *
 * @api
 */
radio_freq_t pktComputeOperatingFrequency(const radio_unit_t radio,
                                          radio_freq_t base_freq,
                                          channel_hz_t step,
                                          radio_ch_t chan,
                                          const radio_mode_t mode) {

  if(base_freq == FREQ_APRS_RECEIVE) {
    /* Get current RX frequency (if valid) and use that. */
    packet_svc_t *handler = pktGetServiceObject(radio);
    if(pktIsReceiveActive(radio)) {
      base_freq = handler->radio_rx_config.base_frequency;
      step = handler->radio_rx_config.step_hz;
      chan = handler->radio_rx_config.channel;
    } else
      base_freq = FREQ_APRS_DYNAMIC;
  }

  if(base_freq == FREQ_APRS_DYNAMIC) {
    /* Get frequency by geofencing. */
    base_freq = getAPRSRegionFrequency();
    /*
     *  If using geofence ignore channel and step for now.
     *  TODO: Could compute base + step + channel and update PKT object?
     */
    chan = 0;
    step = Si446x_STEP_HZ;
    if(base_freq == FREQ_APRS_SCAN && mode == RADIO_RX) {
      base_freq = Si446x_BASE_FREQ;
      step = Si446x_STEP_HZ;
    }
  }

  /* Calculate operating frequency. */
  uint32_t op_freq = base_freq + (step * chan);
  return pktIsRadioInBand(radio, op_freq) ? op_freq : FREQ_RADIO_INVALID;
}

/**
 * @brief   Check if requested frequency is in band for the radio.
 *
 * @param[in] radio   radio unit ID.
 * @param[in] freq    Radio frequency.
 *
 * @api
 */
bool pktIsRadioInBand(const radio_unit_t radio, const radio_freq_t freq) {
  /* TODO: Mapping of radio ID to radio device/capabilities. */
  (void)radio;
  return (Si446x_MIN_FREQ <= freq && freq < Si446x_MAX_FREQ);
}

/**
 * @brief   Send on radio.
 * @notes   This is the API interface to the radio LLD.
 * @notes   Currently just map directly to 446x driver.
 * @notes   In future would implement a lookup and VMT to access radio methods.
 *
 * @param[in] rto radio task object pointer.
 *
 * @notapi
 */
bool pktLLDsendPacket(radio_task_object_t *rto) {
  bool status;
  switch(rto->type) {
  case MOD_2FSK:
    status = Si446x_blocSend2FSK(rto);
    break;

  case MOD_AFSK:
    status = Si446x_blocSendAFSK(rto);
    break;

  case MOD_NONE:
    status = false;
  } /* End switch on task_object->type. */
  return status;
}

/**
 * @brief   Resume reception paused by transmit task.
 * @notes   This is the API interface to the radio LLD.
 * @notes   Currently just map directly to 446x driver.
 * @notes   In future would implement a lookup and VMT to access radio methods.
 *
 * @param[in] radio radio unit ID.
 *
 * @return  status of the operation
 * @retval  true    operation succeeded.
 * retval   false   operation failed.
 *
 * @notapi
 */
bool pktLLDresumeReceive(const radio_unit_t radio) {
  packet_svc_t *handler = pktGetServiceObject(radio);

  chDbgAssert(handler != NULL, "invalid radio ID");

  radio_freq_t freq = handler->radio_rx_config.base_frequency;
  channel_hz_t step = handler->radio_rx_config.step_hz;
  radio_ch_t chan = handler->radio_rx_config.channel;
  radio_squelch_t rssi = handler->radio_rx_config.squelch;
  mod_t mod = handler->radio_rx_config.type;
  return Si4464_resumeReceive(radio, freq, step, chan, rssi, mod);
}

/** @} */
