#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

namespace frame
{
    namespace ob
    {
        class OrderQNode
        {

        private:

            OrderQNode(const OrderQNode&);

        public:

            OrderQNode*prev;
            OrderQNode*next;
            int64_t id; // all orders must be indexable by id

        protected:

            OrderQNode()
            {
                prev=next=0;
                id=0;
            }

            virtual ~OrderQNode() {}

        };
    }
}
