#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

namespace frame {
  namespace ob {
    namespace act {

      class OB_Abstract {
      public:
        virtual ~OB_Abstract() = default;

        // Pure virtual functions for symbol and exchange ID management
        virtual int get_ex_id(int sym) = 0;
        virtual uint get_sym() const = 0;
        virtual int get_ex_sym_id() = 0;
        virtual void set_ex_sym_id(uint id) = 0;
      };

    }
  }
}