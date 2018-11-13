/*
    This file is part of Kismet

    Kismet is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Kismet is distributed in the hope that it will be useful,
      but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Kismet; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "config.h"

#include "devicetracker_view.h"
#include "devicetracker_component.h"
#include "util.h"

#include "kis_mutex.h"
#include "kismet_algorithm.h"

DevicetrackerView::DevicetrackerView(const std::string& in_id, new_device_cb in_new_cb,
        updated_device_cb in_update_cb) :
    tracker_component{},
    new_cb {in_new_cb},
    update_cb {in_update_cb} {

    using namespace std::placeholders;

    register_fields();
    reserve_fields(nullptr);

    set_view_id(in_id);

    auto uri = fmt::format("/devices/view/{}/devices", in_id);

    device_endp =
        std::make_shared<Kis_Net_Httpd_Simple_Post_Endpoint>(uri, false,
                std::bind(&DevicetrackerView::device_endpoint_handler, this, _1, _2), &mutex);
}

std::shared_ptr<TrackerElementVector> DevicetrackerView::doDeviceWork(DevicetrackerViewWorker& worker) {
    // Make a copy of the vector
    local_demand_locker dl(&mutex);
    dl.lock();
    auto immutable_copy = std::make_shared<TrackerElementVector>(device_list);
    dl.unlock();

    return doDeviceWork(worker, immutable_copy);
}

std::shared_ptr<TrackerElementVector> DevicetrackerView::doDeviceWork(DevicetrackerViewWorker& worker,
        std::shared_ptr<TrackerElementVector> devices) {
    auto ret = std::make_shared<TrackerElementVector>();

    kismet__for_each(devices->begin(), devices->end(),
            [&](SharedTrackerElement val) {

            if (val == nullptr)
                return;

            auto dev = std::static_pointer_cast<kis_tracked_device_base>(val);

            bool m;
            {
                local_locker devlocker(dev->device_mutex);
                m = worker.matchDevice(dev);
            }

            if (m)
                ret->push_back(dev);
            });

    worker.setMatchedDevices(ret);

    return ret;
}

void DevicetrackerView::newDevice(std::shared_ptr<kis_tracked_device_base> device) {
    local_locker l(mutex);

    if (new_cb != nullptr)
        if (new_cb(device))
            device_list->push_back(device);
}

void DevicetrackerView::updateDevice(std::shared_ptr<kis_tracked_device_base> device) {
    local_locker l(mutex);

    if (update_cb == nullptr)
        return;

    bool retain = update_cb(device);
    auto dpmi = device_presence_map.find(device->get_key());

    // If we're adding the device (or keeping it) and we don't have it tracked,
    // add it and record it in the presence map
    if (retain && dpmi == device_presence_map.end()) {
        device_list->push_back(device);
        device_presence_map[device->get_key()] = true;
    }

    // if we're removing the device, find it in the vector and remove it, and remove
    // it from the presence map; this is expensive
    if (!retain && dpmi != device_presence_map.end()) {
        for (auto di = device_list->begin(); di != device_list->end(); ++di) {
            if (*di == device) {
                device_list->erase(di);
                break;
            }
        }
        device_presence_map.erase(dpmi);
    }
}

void DevicetrackerView::removeDevice(std::shared_ptr<kis_tracked_device_base> device) {
    local_locker l(mutex);

    auto di = device_presence_map.find(device->get_key());

    if (di != device_presence_map.end()) {
        device_presence_map.erase(di);

        for (auto vi = device_list->begin(); vi != device_list->end(); ++vi) {
            if (*vi == device) {
                device_list->erase(vi);
                break;
            }
        }
    }
}

unsigned int DevicetrackerView::device_endpoint_handler(std::ostream& stream, 
        SharedStructured structured) {

    return 500;
}


