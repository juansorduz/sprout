/**
 * @file subscriber_manager.cpp
 *
 * Copyright (C) Metaswitch Networks 2018
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include "base_subscriber_manager.h"
#include "subscriber_manager.h"
#include "aor_utils.h"
#include "pjutils.h"

SubscriberManager::SubscriberManager(S4* s4,
                                     HSSConnection* hss_connection,
                                     AnalyticsLogger* analytics_logger,
                                     NotifySender* notify_sender) :
  _s4(s4),
  _hss_connection(hss_connection),
  _analytics(analytics_logger),
  _notify_sender(notify_sender)
{
  if (_s4 != NULL)
  {
    TRC_DEBUG("Initialising S4 with reference to this subscriber manager");
    _s4->initialise(this);
  }
}

SubscriberManager::~SubscriberManager()
{
  // EM-TODO: temp line to fix memory leaks in the UTs
  delete _notify_sender; _notify_sender = NULL;
}


HTTPCode SubscriberManager::register_subscriber(const std::string& aor_id,
                                                const std::string& server_name,
                                                const AssociatedURIs& associated_uris,
                                                const Bindings& add_bindings,
                                                Bindings& all_bindings,
                                                SAS::TrailId trail)
{
  // We are registering a subscriber for the first time, so there is no stored
  // AoR. PUT the new bindings to S4.
  AoR* orig_aor = NULL;
  AoR* updated_aor = NULL;
  HTTPCode rc = put_bindings(aor_id,
                             add_bindings,
                             associated_uris,
                             server_name,
                             updated_aor,
                             trail);

  // The PUT failed, so return.
  if (rc != HTTP_OK)
  {
    delete orig_aor; orig_aor = NULL;
    delete updated_aor; updated_aor = NULL;
    return rc;
  }

  // Get all bindings to return to the caller
  all_bindings = AoRUtils::copy_bindings(updated_aor->bindings());

  // Send NOTIFYs and write audit logs.
  //send_notifys_and_write_audit_logs(aor_id,
    //                                EventTrigger::USER,
      //                              orig_aor,
        //                            updated_aor,
         //                           trail);

  // Update HSS if all bindings expired.
  // TODO make sure this is impossible before deleting.
  /*if (all_bindings.empty())
  {
    rc = deregister_with_hss(aor_id,
                             HSSConnection::DEREG_USER,
                             irs_query._server_name,
                             irs_info,
                             trail);
  }*/

  // Send 3rd party REGISTERs.

  delete orig_aor; orig_aor = NULL;
  delete updated_aor; updated_aor = NULL;

  return HTTP_OK;
}

HTTPCode SubscriberManager::reregister_subscriber(const std::string& aor_id,
                                                  const AssociatedURIs& associated_uris,
                                                  const Bindings& updated_bindings,
                                                  const std::vector<std::string>& binding_ids_to_remove,
                                                  Bindings& all_bindings,
                                                  HSSConnection::irs_info& irs_info,
                                                  SAS::TrailId trail)
{
  // Get the current AoR from S4.
  AoR* orig_aor = NULL;
  uint64_t unused_version;
  HTTPCode rc = _s4->handle_get(aor_id,
                                &orig_aor,
                                unused_version,
                                trail);

  // We are reregistering a subscriber, so there must be an existing AoR in the
  // store.
  if (rc != HTTP_OK)
  {
    delete orig_aor; orig_aor = NULL;
    return rc;
  }

  // Check if there are any subscriptions that share the same contact as
  // the removed bindings, and delete them too.
  std::vector<std::string> subscription_ids_to_remove =
                             subscriptions_to_remove(orig_aor->bindings(),
                                                     orig_aor->subscriptions(),
                                                     updated_bindings,
                                                     binding_ids_to_remove);

  // PATCH the existing AoR.
  AoR* updated_aor = NULL;
  rc = patch_bindings(aor_id,
                      updated_bindings,
                      binding_ids_to_remove,
                      subscription_ids_to_remove,
                      associated_uris,
                      updated_aor,
                      trail);

  // The PATCH failed, so return.
  if (rc != HTTP_OK)
  {
    delete orig_aor; orig_aor = NULL;
    delete updated_aor; updated_aor = NULL;
    return rc;
  }

  // SS5-TODO: log increased bindings/subscriptions.

  // Get all bindings to return to the caller
  all_bindings = AoRUtils::copy_bindings(updated_aor->bindings());

  // Send NOTIFYs and write audit logs.
//  send_notifys(aor_id,
  //             SubscriberDataUtils::EventTrigger::USER,
    //           orig_aor,
      //         updated_aor,
        //       trail);

  // Update HSS if all bindings expired.
  if (all_bindings.empty())
  {
    rc = deregister_with_hss(aor_id,
                             HSSConnection::DEREG_USER,
                             updated_aor->_scscf_uri,
                             irs_info,
                             trail);
  }

  // Send 3rd party REGISTERs.

  delete orig_aor; orig_aor = NULL;
  delete updated_aor; updated_aor = NULL;

  return HTTP_OK;
}

HTTPCode SubscriberManager::remove_bindings(const std::string& public_id,
                                            const std::vector<std::string>& binding_ids,
                                            const SubscriberDataUtils::EventTrigger& event_trigger,
                                            Bindings& bindings,
                                            SAS::TrailId trail)
{
  // Get cached subscriber information from the HSS.
  std::string aor_id;
  HSSConnection::irs_info irs_info;
  HTTPCode rc = get_cached_default_id(public_id,
                                      aor_id,
                                      irs_info,
                                      trail);
  if (rc != HTTP_OK)
  {
    return rc;
  }

  // Get the original AoR from S4.
  AoR* orig_aor = NULL;
  uint64_t unused_version;
  rc = _s4->handle_get(aor_id,
                       &orig_aor,
                       unused_version,
                       trail);

  // If there is no AoR, we still count that as a success.
  if (rc != HTTP_OK)
  {
    delete orig_aor; orig_aor = NULL;
    if (rc == HTTP_NOT_FOUND)
    {
      return HTTP_OK;
    }

    return rc;
  }

  // Check if there are any subscriptions that share the same contact as
  // the removed bindings, and delete them too.
  std::vector<std::string> subscription_ids_to_remove =
                             subscriptions_to_remove(orig_aor->bindings(),
                                                     orig_aor->subscriptions(),
                                                     Bindings(),
                                                     binding_ids);

  AoR* updated_aor = NULL;
  rc = patch_bindings(aor_id,
                      Bindings(),
                      binding_ids,
                      subscription_ids_to_remove,
                      irs_info._associated_uris,
                      updated_aor,
                      trail);
  if (rc != HTTP_OK)
  {
    delete orig_aor; orig_aor = NULL;
    delete updated_aor; updated_aor = NULL;
    return rc;
  }

  // Get all bindings to return to the caller
  bindings = AoRUtils::copy_bindings(updated_aor->bindings());

  // Send NOTIFYs for removed bindings.
//  send_notifys_and_write_audit_logs(aor_id,
 //                                   event_trigger,
  //                                  orig_aor,
   //                                 updated_aor,
    //                                trail);

  // Update HSS if all bindings expired.
  if (bindings.empty())
  {
    std::string dereg_reason = (event_trigger == SubscriberDataUtils::EventTrigger::USER) ?
                                 HSSConnection::DEREG_USER : HSSConnection::DEREG_ADMIN;
    rc = deregister_with_hss(aor_id,
                             dereg_reason,
                             updated_aor->_scscf_uri,
                             irs_info,
                             trail);

    // Send 3rd party deREGISTERs.
  }
  else
  {
    // Send 3rd party REGISTERs
  }

  delete orig_aor; orig_aor = NULL;
  delete updated_aor; updated_aor = NULL;

  return HTTP_OK;
}

HTTPCode SubscriberManager::update_subscription(const std::string& public_id,
                                                const SubscriptionPair& subscription,
                                                HSSConnection::irs_info& irs_info,
                                                SAS::TrailId trail)
{
  return modify_subscription(public_id,
                             subscription,
                             "",
                             irs_info,
                             trail);
}

HTTPCode SubscriberManager::remove_subscription(const std::string& public_id,
                                                const std::string& subscription_id,
                                                HSSConnection::irs_info& irs_info,
                                                SAS::TrailId trail)
{
  return modify_subscription(public_id,
                             SubscriptionPair(),
                             subscription_id,
                             irs_info,
                             trail);
}

HTTPCode SubscriberManager::deregister_subscriber(const std::string& public_id,
                                                  const SubscriberDataUtils::EventTrigger& event_trigger,
                                                  SAS::TrailId trail)
{
  // Get cached subscriber information from the HSS.
  std::string aor_id;
  HSSConnection::irs_info irs_info;
  HTTPCode rc = get_cached_default_id(public_id,
                                      aor_id,
                                      irs_info,
                                      trail);
  if (rc != HTTP_OK)
  {
    return rc;
  }

  // Get the original AoR from S4.
  AoR* orig_aor = NULL;
  do
  {
    uint64_t version;
    rc = _s4->handle_get(aor_id,
                         &orig_aor,
                         version,
                         trail);

    // If there is no AoR, we still count that as a success.
    if (rc != HTTP_OK)
    {
      delete orig_aor; orig_aor = NULL;
      if (rc == HTTP_NOT_FOUND)
      {
        return HTTP_OK;
      }

      return rc;
    }

    rc = _s4->handle_delete(aor_id,
                            version,
                            trail);
  } while (rc == HTTP_PRECONDITION_FAILED);

  if ((rc != HTTP_OK) && (rc != HTTP_NO_CONTENT))
  {
    delete orig_aor; orig_aor = NULL;
    return rc;
  }

  // Send NOTIFYs and write audit logs.
//  send_notifys_and_write_audit_logs(aor_id,
//                                    event_trigger,
//                                    orig_aor,
//                                    NULL,
 //                                   trail);

  // Deregister with HSS.
  std::string dereg_reason = (event_trigger == SubscriberDataUtils::EventTrigger::USER) ?
                               HSSConnection::DEREG_USER : HSSConnection::DEREG_ADMIN;
  rc = deregister_with_hss(aor_id,
                           dereg_reason,
                           orig_aor->_scscf_uri,
                           irs_info,
                           trail);

  // Send 3rd party deREGISTERs.

  delete orig_aor; orig_aor = NULL;

  return HTTP_OK;
}

HTTPCode SubscriberManager::get_bindings(const std::string& public_id,
                                         Bindings& bindings,
                                         SAS::TrailId trail)
{
  // Get the current AoR from S4.
  // TODO make sure this only returns not expired bindings.
  AoR* aor = NULL;
  uint64_t unused_version;
  HTTPCode rc = _s4->handle_get(public_id,
                                &aor,
                                unused_version,
                                trail);
  if (rc != HTTP_OK)
  {
    return rc;
  }

  // Set the bindings to return to the caller.
  bindings = AoRUtils::copy_bindings(aor->bindings());

  delete aor; aor = NULL;
  return HTTP_OK;
}

HTTPCode SubscriberManager::get_subscriptions(const std::string& public_id,
                                              Subscriptions& subscriptions,
                                              SAS::TrailId trail)
{
  // Get the current AoR from S4.
  // TODO make sure this only returns not expired subscriptions.
  AoR* aor = NULL;
  uint64_t unused_version;
  HTTPCode rc = _s4->handle_get(public_id,
                                &aor,
                                unused_version,
                                trail);
  if (rc != HTTP_OK)
  {
    return rc;
  }

  // Set the subscriptions to return to the caller.
  subscriptions = AoRUtils::copy_subscriptions(aor->subscriptions());

  delete aor; aor = NULL;
  return HTTP_OK;
}

HTTPCode SubscriberManager::get_cached_subscriber_state(const std::string& public_id,
                                                        HSSConnection::irs_info& irs_info,
                                                        SAS::TrailId trail)
{
  HTTPCode http_code = _hss_connection->get_registration_data(public_id,
                                                              irs_info,
                                                              trail);
  return http_code;
}

HTTPCode SubscriberManager::get_subscriber_state(const HSSConnection::irs_query& irs_query,
                                                 HSSConnection::irs_info& irs_info,
                                                 SAS::TrailId trail)
{
  HTTPCode http_code = _hss_connection->update_registration_state(irs_query,
                                                                  irs_info,
                                                                  trail);
  return http_code;
}

HTTPCode SubscriberManager::update_associated_uris(const std::string& aor_id,
                                                   const AssociatedURIs& associated_uris,
                                                   SAS::TrailId trail)
{
  // Get the original AoR from S4.
  AoR* orig_aor = NULL;
  uint64_t unused_version;
  HTTPCode rc = _s4->handle_get(aor_id,
                                &orig_aor,
                                unused_version,
                                trail);

  if (rc != HTTP_OK)
  {
    return rc;
  }

  AoR* updated_aor = NULL;
  rc = patch_associated_uris(aor_id,
                             associated_uris,
                             updated_aor,
                             trail);

  if (rc != HTTP_OK)
  {
    delete orig_aor; orig_aor = NULL;
    return rc;
  }

  // Send NOTIFYs and write audit logs.
//  send_notifys_and_write_audit_logs(aor_id,
 //                                   SubscriberDataUtils::EventTrigger::ADMIN,
  //                                  orig_aor,
   //                                 updated_aor,
    //                                trail);

  // Send 3rd party REGISTERs?

  delete orig_aor; orig_aor = NULL;
  delete updated_aor; updated_aor = NULL;

  return HTTP_OK;
}

HTTPCode SubscriberManager::modify_subscription(const std::string& public_id,
                                                const SubscriptionPair& update_subscription,
                                                const std::string& remove_subscription,
                                                HSSConnection::irs_info& irs_info,
                                                SAS::TrailId trail)
{
  // Get cached subscriber information from the HSS.
  std::string aor_id;
  HTTPCode rc = get_cached_default_id(public_id,
                                      aor_id,
                                      irs_info,
                                      trail);
  if (rc != HTTP_OK)
  {
    return rc;
  }

  // Get the current AoR from S4.
  AoR* orig_aor = NULL;
  uint64_t unused_version;
  rc = _s4->handle_get(aor_id,
                       &orig_aor,
                       unused_version,
                       trail);

  // There must be an existing AoR since there must be bindings to subscribe to.
  if (rc != HTTP_OK)
  {
    return rc;
  }

  AoR* updated_aor = NULL;
  rc = patch_subscription(aor_id,
                          update_subscription,
                          remove_subscription,
                          updated_aor,
                          trail);
  if (rc != HTTP_OK)
  {
    delete orig_aor; orig_aor = NULL;
    return rc;
  }

  // Send NOTIFYs and write audit logs.
//  send_notifys_and_write_audit_logs(aor_id,
 //                                   SubscriberDataUtils::EventTrigger::USER,
  //                                  orig_aor,
   //                                 updated_aor,
    //                                trail);

  delete orig_aor; orig_aor = NULL;
  delete updated_aor; updated_aor = NULL;

  return HTTP_OK;
}

HTTPCode SubscriberManager::get_cached_default_id(const std::string& public_id,
                                                  std::string& aor_id,
                                                  HSSConnection::irs_info& irs_info,
                                                  SAS::TrailId trail)
{
  HTTPCode rc = get_cached_subscriber_state(public_id,
                                            irs_info,
                                            trail);
  if (rc != HTTP_OK)
  {
    return rc;
  }

  // Get the aor_id from the associated URIs.
  if (!irs_info._associated_uris.get_default_impu(aor_id, false))
  {
    // TODO No default IMPU - what should we do here? Probably bail out.
    TRC_ERROR("No Default IMPU in IRS");
    return HTTP_BAD_REQUEST;
  }

  return rc;
}

HTTPCode SubscriberManager::put_bindings(const std::string& aor_id,
                                         const Bindings& update_bindings,
                                         const AssociatedURIs& associated_uris,
                                         const std::string& scscf_uri,
                                         AoR*& aor,
                                         SAS::TrailId trail)
{
  PatchObject patch_object;
  patch_object.set_update_bindings(AoRUtils::copy_bindings(update_bindings));
  patch_object.set_associated_uris(associated_uris);
  patch_object.set_increment_cseq(true);

  aor = new AoR(aor_id);
  aor->patch_aor(patch_object);
  aor->_scscf_uri = scscf_uri;
  // TODO Retry with patch if contention (HTTP_PRECONDITION_FAILED)
  HTTPCode rc = _s4->handle_put(aor_id,
                                *aor,
                                trail);

  return rc;
}

HTTPCode SubscriberManager::patch_bindings(const std::string& aor_id,
                                           const Bindings& update_bindings,
                                           const std::vector<std::string>& remove_bindings,
                                           const std::vector<std::string>& remove_subscriptions,
                                           const AssociatedURIs& associated_uris,
                                           AoR*& aor,
                                           SAS::TrailId trail)
{
  PatchObject patch_object;
  patch_object.set_update_bindings(AoRUtils::copy_bindings(update_bindings));
  patch_object.set_remove_bindings(remove_bindings);
  patch_object.set_remove_subscriptions(remove_subscriptions);
  patch_object.set_associated_uris(associated_uris);
  patch_object.set_increment_cseq(true);
  HTTPCode rc = _s4->handle_patch(aor_id,
                                  patch_object,
                                  &aor,
                                  trail);

  return rc;
}

HTTPCode SubscriberManager::patch_subscription(const std::string& aor_id,
                                               const SubscriptionPair& update_subscription,
                                               const std::string& remove_subscription,
                                               AoR*& aor,
                                               SAS::TrailId trail)
{
  PatchObject patch_object;
  Subscriptions subscriptions;
  if (update_subscription.second != NULL)
  {
    subscriptions.insert(update_subscription);
  }
  patch_object.set_update_subscriptions(AoRUtils::copy_subscriptions(subscriptions));
  patch_object.set_remove_subscriptions({remove_subscription});
  patch_object.set_increment_cseq(true);
  HTTPCode rc = _s4->handle_patch(aor_id,
                                  patch_object,
                                  &aor,
                                  trail);

  return rc;
}

HTTPCode SubscriberManager::patch_associated_uris(const std::string& aor_id,
                                                  const AssociatedURIs& associated_uris,
                                                  AoR*& aor,
                                                  SAS::TrailId trail)
{
  PatchObject patch_object;
  patch_object.set_associated_uris(associated_uris);
  patch_object.set_increment_cseq(true);
  HTTPCode rc = _s4->handle_patch(aor_id,
                                  patch_object,
                                  &aor,
                                  trail);

  return rc;
}

std::vector<std::string> SubscriberManager::subscriptions_to_remove(const Bindings& orig_bindings,
                                                                    const Subscriptions& orig_subscriptions,
                                                                    const Bindings& bindings_to_update,
                                                                    const std::vector<std::string> binding_ids_to_remove)
{
  std::vector<std::string> subscription_ids_to_remove;
  std::set<std::string> missing_uris;

  // Store off the contact URIs of bindings to be removed.
  for (std::string binding_id : binding_ids_to_remove)
  {
    Bindings::const_iterator b = orig_bindings.find(binding_id);
    if (b != orig_bindings.end())
    {
      missing_uris.insert(b->second->_uri);
    }
  }

  // Store off the original contact URI of bindings where the contact is about
  // to be changed.
  for (BindingPair bp : bindings_to_update)
  {
    Bindings::const_iterator b = orig_bindings.find(bp.first);
    if ((b != orig_bindings.end()) &&
        (b->second->_uri != bp.second->_uri))
    {
      missing_uris.insert(b->second->_uri);
    }
  }

  // Loop over the subscriptions. If any have the same contact as one of the
  // missing URIs, the subscription should be removed.
  for (SubscriptionPair sp : orig_subscriptions)
  {
    if (missing_uris.find(sp.second->_req_uri) != missing_uris.end())
    {
      TRC_DEBUG("Subscription %s is being removed because the binding that shares"
                " its contact URI %s is being removed or changing contact URI",
                sp.first.c_str(),
                sp.second->_req_uri.c_str());
      subscription_ids_to_remove.push_back(sp.first);
    }
  }

  return subscription_ids_to_remove;
}

void classify_bindings_and_subscriptions(std::string aor_id,
                              SubscriberDataUtils::EventTrigger event_trigger,
                              AoR* orig_aor,
                              AoR* updated_aor,
                              SubscriberDataUtils::ClassifiedBindings& classified_bindings,
                              SubscriberDataUtils::ClassifiedSubscriptions& classified_subscriptions)
{
  // Classify bindings.
  SubscriberDataUtils::classify_bindings(aor_id,
                    event_trigger,
                    (orig_aor != NULL) ? orig_aor->bindings() : Bindings(),
                    (updated_aor != NULL) ? updated_aor->bindings() : Bindings(),
                    classified_bindings);

  // Work out if Associated URIs have changed.
  bool associated_uris_changed = false;
  if ((orig_aor != NULL) && (updated_aor != NULL))
  {
    associated_uris_changed = (orig_aor->_associated_uris !=
                               updated_aor->_associated_uris);
  }
  else
  {
    // One of the AoRs is NULL so we are either creating or deleting an AoR.
    // This isn't a change to Associated URIs so don't set it to true.
  }

  SubscriberDataUtils::classify_subscriptions(aor_id,
                         event_trigger,
                         (orig_aor != NULL) ? orig_aor->subscriptions() : Subscriptions(),
                         (updated_aor != NULL) ? updated_aor->subscriptions() : Subscriptions(),
                         classified_bindings,
                         associated_uris_changed,
                         classified_subscriptions);
}

void SubscriberManager::send_notifys(
                        const std::string& aor_id,
                        const SubscriberDataUtils::EventTrigger& event_trigger,
                        const SubscriberDataUtils::ClassifiedBindings& classified_bindings,
                        const SubscriberDataUtils::ClassifiedSubscriptions& classified_subscriptions,
                        AoR* orig_aor,
                        AoR* updated_aor,
                        int now,
                        SAS::TrailId trail)
{
  // Send NOTIFYs. If the updated AoR is NULL e.g. if we have deleted a
  // subscriber, the best we should use the CSeq on the original AoR and
  // increment it by 1.
  _notify_sender->send_notifys(aor_id,
                               classified_bindings,
                               classified_subscriptions,
                               (updated_aor != NULL) ? updated_aor->_associated_uris : orig_aor->_associated_uris,
                               (updated_aor != NULL) ? updated_aor->_notify_cseq : orig_aor->_notify_cseq + 1,
                               now,
                               trail);
}

// TODO Better name!
void SubscriberManager::delete_stuff(
                              SubscriberDataUtils::ClassifiedBindings& classified_bindings,
                              SubscriberDataUtils::ClassifiedSubscriptions& classified_subscriptions)
{
  delete_bindings(classified_bindings);
  delete_subscriptions(classified_subscriptions);
}

void SubscriberManager::log_shortened_bindings(
                                  const SubscriberDataUtils::ClassifiedBindings& classified_bindings,
                                  int now)
{
  for (SubscriberDataUtils::ClassifiedBinding* cb : classified_bindings)
  {
    if ((cb->_contact_event == SubscriberDataUtils::ContactEvent::EXPIRED)     ||
        (cb->_contact_event == SubscriberDataUtils::ContactEvent::DEACTIVATED) ||
        (cb->_contact_event == SubscriberDataUtils::ContactEvent::UNREGISTERED))
    {
      _analytics->registration(cb->_binding->_address_of_record,
                               cb->_id,
                               cb->_binding->_uri,
                               0);
    }
  }
}

void SubscriberManager::log_lengthened_bindings(
                                  const SubscriberDataUtils::ClassifiedBindings& classified_bindings,
                                  int now)
{
  for (SubscriberDataUtils::ClassifiedBinding* cb : classified_bindings)
  {
    if (cb->_contact_event == SubscriberDataUtils::ContactEvent::CREATED ||
        cb->_contact_event == SubscriberDataUtils::ContactEvent::REFRESHED ||
        cb->_contact_event == SubscriberDataUtils::ContactEvent::SHORTENED)

    {
      _analytics->registration(cb->_binding->_address_of_record,
                               cb->_id,
                               cb->_binding->_uri,
                               cb->_binding->_expires - now);
    }
  }
}

void SubscriberManager::log_shortened_subscriptions(
                        const SubscriberDataUtils::ClassifiedSubscriptions& classified_subscriptions,
                        int now)
{
  for (SubscriberDataUtils::ClassifiedSubscription* cs : classified_subscriptions)
  {
    if ((cs->_subscription_event == SubscriberDataUtils::SubscriptionEvent::EXPIRED) ||
        (cs->_subscription_event == SubscriberDataUtils::SubscriptionEvent::TERMINATED))
    {
      _analytics->subscription(cs->_aor_id,
                               cs->_id,
                               cs->_subscription->_req_uri,
                               0);
    }
  }
}

void SubscriberManager::log_lengthened_subscriptions(
                        const SubscriberDataUtils::ClassifiedSubscriptions& classified_subscriptions,
                        int now)
{
  for (SubscriberDataUtils::ClassifiedSubscription* cs : classified_subscriptions)
  {
    if (cs->_subscription_event == SubscriberDataUtils::SubscriptionEvent::CREATED ||
        cs->_subscription_event == SubscriberDataUtils::SubscriptionEvent::REFRESHED ||
        cs->_subscription_event == SubscriberDataUtils::SubscriptionEvent::SHORTENED)
    {
      _analytics->subscription(cs->_aor_id,
                               cs->_id,
                               cs->_subscription->_req_uri,
                               cs->_subscription->_expires - now);
    }
  }
}

HTTPCode SubscriberManager::deregister_with_hss(const std::string& aor_id,
                                                const std::string& dereg_reason,
                                                const std::string& server_name,
                                                HSSConnection::irs_info& irs_info,
                                                SAS::TrailId trail)
{
  HSSConnection::irs_query irs_query;
  irs_query._public_id = aor_id;
  irs_query._req_type = dereg_reason;
  irs_query._server_name = server_name;

  return get_subscriber_state(irs_query, irs_info, trail);
}