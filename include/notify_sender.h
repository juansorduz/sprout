/**
 * @file notify_sender.h
 *
 * Copyright (C) Metaswitch Networks 2018
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef NOTIFY_SENDER_H__
#define NOTIFY_SENDER_H__

extern "C" {
#include <pjsip.h>
#include <pjlib-util.h>
#include <pjlib.h>
#include "pjsip-simple/evsub.h"
#include <pjsip-simple/evsub_msg.h>
}

#include <string>
#include <list>
#include <map>
#include <stdio.h>
#include <stdlib.h>

#include "sas.h"
#include "associated_uris.h"
#include "subscriber_data_utils.h"

/// Notification sender class.
///
/// This class is responsible for sending NOTIFYs to subscribers to reg event
/// state. It understands how to construct valid NOTIFYs, and what NOTIFYs it
/// should send based on how the subscriber reg data has changed.
class NotifySender
{
public:
  NotifySender();

  virtual ~NotifySender();

  // See RFC 3265
  enum class RegistrationState
  {
    ACTIVE,
    TERMINATED
  };

  enum class ContactState
  {
    ACTIVE,
    TERMINATED
  };

  enum class SubscriptionState
  {
    ACTIVE,
    TERMINATED
  };

  enum class ContactEvent
  {
    REGISTERED,
    CREATED,
    REFRESHED,
    SHORTENED,
    EXPIRED,
    DEACTIVATED,
    UNREGISTERED
  };

  /// This compares the original and updated AoRs and sends any NOTIFYs.
  ///
  /// @param aor_id[in]        - The AoR ID.
  /// @param orig_aor[in]      - The original AoR.
  /// @param updated_aor[in]   - The updated AoR.
  /// @param event_trigger[in] - What action triggered this call (can be USER,
  ///                            ADMIN, or TIMEOUT)
  /// @param now[in]           - The current time (used for calculating expiry
  ///                            timers - we want the NotifySender to use the
  ///                            same time as the other components so this is
  ///                            passed in rather than being calculated within
  ///                            the function).
  /// @param trail[in]         - The SAS trail ID.
  virtual void send_notifys(const std::string& aor_id,
                            const AoR& orig_aor,
                            const AoR& updated_aor,
                            SubscriberDataUtils::EventTrigger event_trigger,
                            int now,
                            SAS::TrailId trail);

private:
  pj_status_t create_subscription_notify(
                                  pjsip_tx_data** tdata_notify,
                                  Subscription* s,
                                  const std::string& aor,
                                  const AssociatedURIs& associated_uris,
                                  int cseq,
                                  const ClassifiedBindings& classified_bindings,
                                  const RegistrationState& reg_state,
                                  int now,
                                  SAS::TrailId trail);

  pj_status_t create_notify(pjsip_tx_data** tdata_notify,
                            Subscription* subscription,
                            const std::string& aor,
                            const AssociatedURIs& associated_uris,
                            int cseq,
                            const ClassifiedBindings& classified_bindings,
                            const RegistrationState& reg_state,
                            const SubscriptionState& subscription_state,
                            int expiry,
                            SAS::TrailId trail);

  pj_status_t create_request_from_subscription(pjsip_tx_data** p_tdata,
                                               Subscription* subscription,
                                               int cseq,
                                               pj_str_t* body);

  pj_status_t notify_create_body(pjsip_msg_body* body,
                                  pj_pool_t *pool,
                                  const std::string& aor,
                                  const AssociatedURIs& associated_uris,
                                  Subscription* subscription,
                                  const ClassifiedBindings& classified_bindings,
                                  const RegistrationState& reg_state,
                                  SAS::TrailId trail);

  pj_xml_node* notify_create_reg_state_xml(
                                  pj_pool_t *pool,
                                  const std::string& aor,
                                  const AssociatedURIs& associated_uris,
                                  Subscription* subscription,
                                  const ClassifiedBindings& classified_bindings,
                                  const RegistrationState& reg_state,
                                  SAS::TrailId trail);

  pj_xml_node* create_reg_node(pj_pool_t* pool,
                               pj_str_t* aor,
                               pj_str_t* id,
                               pj_str_t* state);


  pj_xml_node* create_contact_node(pj_pool_t* pool,
                                   pj_str_t* id,
                                   pj_str_t* state,
                                   pj_str_t* event);

};

#endif
