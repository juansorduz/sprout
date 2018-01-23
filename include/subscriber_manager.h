/**
 * @file subscriber_manager.h
 *
 * Copyright (C) Metaswitch Networks 2018
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef SUBSCRIBER_MANAGER_H__
#define SUBSCRIBER_MANAGER_H__

extern "C" {
#include <pjsip.h>
}

#include <string>
#include <list>
#include <map>
#include <stdio.h>
#include <stdlib.h>

#include "aor.h"
#include "sas.h"
#include "analyticslogger.h"
#include "associated_uris.h"
#include "hssconnection.h"
#include "ifchandler.h"
#include "aor.h"
#include "s4.h"
#include "notify_utils.h"

// SDM-REFACTOR-TODO: Add Doxygen comments.
class SubscriberManager
{
public:
  enum EventTrigger
  {
    USER,
    ADMIN
  };

  enum SubscriptionEvent
  {
    CREATED,
    REFRESHED,
    UNCHANGED,
    SHORTENED,
    EXPIRED,
    TERMINATED
  };

  struct ClassifiedSubscription
  {
    ClassifiedSubscription(std::string id,
                           Subscription* subscription,
                           SubscriptionEvent event) :
      _id(id),
      _subscription(subscription),
      _subscription_event(event),
      _notify_required(false),
      _reasons()
    {}

    std::string _id;
    Subscription* _subscription;
    SubscriptionEvent _subscription_event;
    bool _notify_required;
    std::string _reasons; // Stores reasons for requiring a notify (for logging)
  };

  typedef std::vector<ClassifiedSubscription*> ClassifiedSubscriptions;

  class NotifySender
  {
  public:
    NotifySender();

    virtual ~NotifySender();

    /// Create and send any appropriate NOTIFYs
    ///
    /// @param aor_id       The AoR ID
    /// @param associated_uris
    ///                     The IMPUs associated with this IRS
    /// @param aor_pair     The AoR pair to send NOTIFYs for
    /// @param now          The current time
    /// @param trail        SAS trail
    void send_notifys(const std::string& aor_id,
                      const EventTrigger& event_trigger,
                      const ClassifiedBindings& classified_bindings,
                      const ClassifiedSubscriptions& classified_subscriptions,
                      AssociatedURIs& associated_uris, // TODO make const again.
                      int cseq,
                      int now,
                      SAS::TrailId trail);
  };

  /// SubscriberManager constructor.
  ///
  /// @param s4                 - Pointer to the underlying data store interface
  /// @param hss_connection     - Sprout's HSS connection (via homestead)
  /// @param analytics_logger   - AnalyticsLogger for reporting registration events
  SubscriberManager(S4* s4,
                    HSSConnection* hss_connection,
                    AnalyticsLogger* analytics_logger);

  /// Destructor.
  virtual ~SubscriberManager();

  /// Updates the bindings stored in SM for a given public ID.
  ///
  /// @param[in]  irs_query     The IRS query object to use to query the HSS
  /// @param[in]  updated_bindings
  ///                           The bindings to update
  /// @param[in]  binding_ids_to_remove
  ///                           The binding IDs to remove
  /// @param[out] all_bindings  All bindings currently stores for this public ID
  /// @param[out] irs_info      The IRS information stored about this public ID
  /// @param[in]  trail         The SAS trail ID
  virtual HTTPCode update_bindings(const HSSConnection::irs_query& irs_query,
                                   const Bindings& updated_bindings,
                                   const std::vector<std::string>& binding_ids_to_remove,
                                   Bindings& all_bindings,
                                   HSSConnection::irs_info& irs_info,
                                   SAS::TrailId trail);

  /// Removes bindings stored in SM for a given public ID.
  ///
  /// @param[in]  public_id     The public IDs to remove bindings for
  /// @param[in]  binding_ids   The binding IDs to remove
  /// @param[in]  event_trigger The reason for removing bindings
  /// @param[out] bindings      All bindings currently stores for this public ID
  /// @param[in]  trail         The SAS trail ID
  virtual HTTPCode remove_bindings(const std::string& public_id,
                                   const std::vector<std::string>& binding_ids,
                                   const EventTrigger& event_trigger,
                                   Bindings& bindings,
                                   SAS::TrailId trail);

  /// Updates a subscription stored in SM for a given public ID.
  ///
  /// @param[in]  public_id     The public ID being subscribed to
  /// @param[in]  subscription  A pair containing the subscription ID and
  ///                           subscription to update
  /// @param[out] irs_info      The IRS information stored about this public ID
  /// @param[in]  trail         The SAS trail ID
  virtual HTTPCode update_subscription(const std::string& public_id,
                                       const SubscriptionPair& subscription,
                                       HSSConnection::irs_info& irs_info,
                                       SAS::TrailId trail);

  /// Removes a subscription stored in SM for a given public ID.
  ///
  /// @param[in]  public_id     The public ID subscribed to
  /// @param[in]  subscription  The subscription ID to remove
  /// @param[out] irs_info      The IRS information stored about this public ID
  /// @param[in]  trail         The SAS trail ID
  virtual HTTPCode remove_subscription(const std::string& public_id,
                                       const std::string& subscription_id,
                                       HSSConnection::irs_info& irs_info,
                                       SAS::TrailId trail);

  /// Deregisters a subscriber completely.
  ///
  /// @param[in]  public_id     The public ID to deregister
  /// @param[in]  event_trigger The reason for deregistering the subscriber
  /// @param[in]  trail         The SAS trail ID
  virtual HTTPCode deregister_subscriber(const std::string& public_id,
                                         const EventTrigger& event_trigger,
                                         SAS::TrailId trail);

  /// Gets all bindings stored for a given AoR ID.
  ///
  /// @param[in]  aor_id        The AoR ID to lookup in the store. It is the
  ///                           client's responsibilty to provide an ID that
  ///                           will be found in the store i.e. a default public
  ///                           ID
  ///                           Providing a non-default IMPU from an IRS will
  ///                           NOT return all binidngs
  /// @param[out] bindings      All bindings stored for this AoR
  /// @param[in]  trail         The SAS trail ID
  virtual HTTPCode get_bindings(const std::string& aor_id,
                                Bindings& bindings,
                                SAS::TrailId trail);

  /// Gets all bindings and subscriptions stored for a given AoR ID.
  ///
  /// @param[in]  aor_id        The AoR ID to lookup in the store. It is the
  ///                           client's responsibilty to provide an ID that
  ///                           will be found in the store i.e. a default public
  ///                           ID
  ///                           Providing a non-default IMPU from an IRS will
  ///                           NOT return all subscriptions
  /// @param[out] subscriptions All subscriptions stored for this AoR
  /// @param[in]  trail         The SAS trail ID
  virtual HTTPCode get_subscriptions(const std::string& aor_id,
                                     Subscriptions& subscriptions,
                                     SAS::TrailId trail);

  /// Gets the cached subscriber state for a given public ID.
  ///
  /// @param[in]  public_id     The public ID to get cached state for
  /// @param[out] irs_info      The cached IRS information for this public ID
  /// @param[in]  trail         The SAS trail ID
  virtual HTTPCode get_cached_subscriber_state(const std::string& public_id,
                                               HSSConnection::irs_info& irs_info,
                                               SAS::TrailId trail);

  /// Gets the subscriber state for a given public ID. This is different to
  /// get_cached_subscriber_state() because it can result in a call to the HSS
  /// if Homestead does not have the information cached.
  ///
  /// @param[in]  public_id     The public ID to get state for
  /// @param[out] irs_info      The IRS information for this public ID
  /// @param[in]  trail         The SAS trail ID
  virtual HTTPCode get_subscriber_state(const HSSConnection::irs_query& irs_query,
                                        HSSConnection::irs_info& irs_info,
                                        SAS::TrailId trail);

  /// Update the associated URIs stored in an AoR.
  /// @param[in]  aor_id        The AoR ID to lookup in the store. It is the
  ///                           client's responsibilty to provide an ID that
  ///                           will be found in the store i.e. a default public
  ///                           ID
  ///                           Providing a non-default IMPU from an IRS will
  ///                           NOT result in the associated URIs being updated
  /// @param[in]  associated_uris
  ///                           The new associated URIs
  /// @param[in]  trail         The SAS trail ID
  virtual HTTPCode update_associated_uris(const std::string& aor_id,
                                          const AssociatedURIs& associated_uris,
                                          SAS::TrailId trail);
private:
  S4* _s4;
  HSSConnection* _hss_connection;
  AnalyticsLogger* _analytics;
  NotifySender* _notify_sender;

  HTTPCode modify_subscription(const std::string& public_id,
                               const SubscriptionPair& update_subscription,
                               const std::string& remove_subscription,
                               HSSConnection::irs_info& irs_info,
                               SAS::TrailId trail);

  HTTPCode get_cached_default_id(const std::string& public_id,
                                 std::string& aor_id,
                                 HSSConnection::irs_info& irs_info,
                                 SAS::TrailId trail);

  HTTPCode patch_bindings(const std::string& aor_id,
                          const Bindings& update_bindings,
                          const std::vector<std::string>& remove_bindings,
                          AoR*& aor,
                          SAS::TrailId trail);

  HTTPCode patch_subscription(const std::string& aor_id,
                              const SubscriptionPair& update_subscription,
                              const std::string& remove_subscription,
                              AoR*& aor,
                              SAS::TrailId trail);

  HTTPCode patch_associated_uris(const std::string& aor_id,
                                 const AssociatedURIs& associated_uris,
                                 AoR*& aor,
                                 SAS::TrailId trail);

  HTTPCode deregister_with_hss(const std::string& aor_id,
                               const std::string& dereg_reason,
                               const std::string& server_name,
                               HSSConnection::irs_info& irs_info,
                               SAS::TrailId trail);

  // Iterate over all original and current bindings in an AoR pair and
  // classify them as removed ("EXPIRED"), created ("CREATED"), refreshed ("REFRESHED"),
  // shortened ("SHORTENED") or unchanged ("REGISTERED").
  //
  // @param aor_id                The AoR ID
  // @param aor_pair              The AoR pair to compare and classify bindings for
  // @param classified_bindings   Output vector of classified bindings
  void classify_bindings(const std::string& aor_id,
                         const EventTrigger& event_trigger,
                         const Bindings& orig_bindings,
                         const Bindings& updated_bindings,
                         ClassifiedBindings& classified_bindings);

  void classify_subscriptions(const std::string& aor_id,
                              const EventTrigger& event_trigger,
                              const Subscriptions& orig_subscriptions,
                              const Subscriptions& updated_subscriptions,
                              const ClassifiedBindings& classified_bindings,
                              const bool& associated_uris_changed,
                              ClassifiedSubscriptions& classified_subscriptions);

  void delete_bindings(ClassifiedBindings& classified_bindings);
  void delete_subscriptions(ClassifiedSubscriptions& classified_subscriptions);

  NotifyUtils::ContactEvent determine_contact_event(const EventTrigger& event_trigger);

};

#endif