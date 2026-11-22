#pragma once

#include <hyprtavern-client/BusObject.hpp>

#include <common/Memory.hpp>

#include <hp_hyprtavern_core_v1-client.hpp>

#include <functional>
#include <optional>
#include <string>

namespace Hyprtavern {
    // redeems a connection principal token for the admission snapshot it retains
    class CPrincipalRedemption {
      public:
        using DoneFn = std::function<void(std::optional<SPrincipal>)>;

        CPrincipalRedemption(const SP<CCHpHyprtavernCoreManagerV1Object>& manager, const std::string& token, DoneFn&& done);

        // whether the principal is inert, and its callback has returned
        bool done() const;

      private:
        void                                          finish(bool redeemed);

        SP<CCHpHyprtavernConnectionPrincipalV1Object> m_principal;
        SPrincipal                                    m_data;
        DoneFn                                        m_done;
        bool                                          m_finished = false;
    };
}
