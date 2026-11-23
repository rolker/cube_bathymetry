// Copyright 2026 Center for Coastal and Ocean Mapping & NOAA-UNH Joint
// Hydrographic Center, University of New Hampshire
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#ifndef MAKE_TRANSFORM_LISTENER_HPP_
#define MAKE_TRANSFORM_LISTENER_HPP_

#include <memory>
#include <type_traits>

#include "tf2/buffer_core.hpp"
#include "tf2_ros/transform_listener.hpp"

namespace cube_bathymetry
{

/// True when ListenerT has the node-interfaces constructor of Rolling's
/// tf2_ros::TransformListener (it names its RequiredInterfaces); false on Jazzy.
template<class ListenerT, class = void>
struct TakesNodeInterfaces : std::false_type {};

template<class ListenerT>
struct TakesNodeInterfaces<ListenerT, std::void_t<typename ListenerT::RequiredInterfaces>>
  : std::true_type {};

/// A TransformListener whose /tf and /tf_static subscriptions belong to `node`, in a
/// callback group of their own that the listener spins on a dedicated thread, outside
/// the node's executor (spin_thread = true, as Jazzy's `TransformListener(buffer, this)`
/// gave).
///
/// The call it replaces, `TransformListener(buffer, this)`, still compiles on Rolling
/// but means something else there: Rolling's listener takes either
/// `(BufferCore &, bool spin_thread, bool static_only)` or `(BufferCore &,
/// RequiredInterfaces, bool spin_thread, ...)`, the interfaces are built from a node
/// reference only, so a node pointer converts to `bool spin_thread` and the listener
/// makes a hidden node of its own (rolker/unh_marine_autonomy#415, cause 14). Jazzy's
/// template constructor dereferences its argument as a pointer, so a node reference
/// does not compile there. Hence the switch, on the listener type so that each branch
/// depends on the template parameter and the one not taken is never resolved.
template<class ListenerT = tf2_ros::TransformListener, class NodeT>
std::shared_ptr<ListenerT> makeTransformListener(tf2::BufferCore & buffer, NodeT & node)
{
  // A `Node *` argument would select Rolling's (BufferCore &, bool spin_thread,
  // bool static_only) constructor without a word.
  static_assert(!std::is_pointer_v<NodeT>, "pass the node by reference");
  constexpr bool spin_thread = true;
  if constexpr (TakesNodeInterfaces<ListenerT>::value) {
    return std::make_shared<ListenerT>(buffer, node, spin_thread);
  } else {
    return std::make_shared<ListenerT>(buffer, &node, spin_thread);
  }
}

}  // namespace cube_bathymetry

#endif  // MAKE_TRANSFORM_LISTENER_HPP_
