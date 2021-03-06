/*
    Aerospace Decoder - Copyright (C) 2018 Bob Anderson (VK2GJ)

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*/

/**
 * @file    pktconf.c
 * @brief   Configuration data.
 *
 * @addtogroup pktconfig
 * @details Decoder related settings and configuration.
 * @{
 */


#include "pktconf.h"

/*===========================================================================*/
/* Module exported variables.                                                */
/*===========================================================================*/

packet_svc_t RPKTD1;

binary_semaphore_t diag_out_sem;
