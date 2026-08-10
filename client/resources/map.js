var map = null;
var userMarker = null;
var bikeMarkers = {};  // bike_no -> marker
var trajPolyline = null;
var trajMarkers = [];

new QWebChannel(qt.webChannelTransport, function(channel) {
    window.bridge = channel.objects.bridge;
    initMap();
});

function initMap() {
    map = L.map('map').setView([39.9821, 116.3145], 15);
    L.tileLayer('https://webrd0{s}.is.autonavi.com/appmaptile?lang=zh_cn&size=1&scale=1&style=8&x={x}&y={y}&z={z}', {
        subdomains: ['1','2','3','4'],
        maxZoom: 18,
        attribution: '© AMap'
    }).addTo(map);
}

// 由 C++ MapBridge 调用
function setUserLocation(lat, lng) {
    if (userMarker) map.removeLayer(userMarker);
    userMarker = L.marker([lat, lng], {
        icon: L.divIcon({className: 'user-icon', iconSize: [16, 16]})
    }).addTo(map);
    // 越界才平移: 常驻定位每 2-3 秒推送一次, 无条件 panTo 会打断
    // 用户手动拖图; 定位在视口内时保持用户当前视角。
    if (!map.getBounds().contains([lat, lng])) {
        map.panTo([lat, lng], {animate: true});
    }
}

function renderBikes(bikes) {
    for (var no in bikeMarkers) {
        map.removeLayer(bikeMarkers[no]);
    }
    bikeMarkers = {};
    var idleCount = 0, damagedCount = 0;
    bikes.forEach(function(b) {
        var cls = b.status === 2 ? 'bike-icon-damaged' : 'bike-icon-idle';
        if (b.status === 2) damagedCount++; else idleCount++;
        var m = L.marker([b.lat, b.lng], {
            icon: L.divIcon({className: cls, iconSize: [18, 18]})
        }).addTo(map);
        m.bindTooltip(b.bike_no);
        m.on('click', function() {
            window.bridge.onBikeClicked(b.bike_no);
        });
        bikeMarkers[b.bike_no] = m;
    });
    // 兜底: 若绘制完成后视口内没有任何车辆标记(如定位远离初始视角),
    // 适配到 车辆点集 ∪ 用户位置, 避免用户看到空地图。空集时不动视角。
    var bounds = L.latLngBounds();
    bikes.forEach(function(b) { bounds.extend([b.lat, b.lng]); });
    if (userMarker) bounds.extend(userMarker.getLatLng());
    if (bounds.isValid()) {
        var vb = map.getBounds();
        var anyVisible = false;
        for (var no in bikeMarkers) {
            if (vb.contains(bikeMarkers[no].getLatLng())) { anyVisible = true; break; }
        }
        if (!anyVisible) map.fitBounds(bounds, {padding: [40, 40]});
    }
    // 经 slot 通知 C++(信号在 WebChannel JS 侧只能 connect 不能调用)
    if (window.bridge && window.bridge.onBikeCountsUpdated) {
        window.bridge.onBikeCountsUpdated(idleCount, damagedCount);
    }
}

function appendTrajectory(lat, lng) {
    if (!trajPolyline) {
        trajPolyline = L.polyline([[lat, lng]], {color: '#2563eb', weight: 4}).addTo(map);
    } else {
        trajPolyline.addLatLng([lat, lng]);
    }
    map.panTo([lat, lng], {animate: true});
}

function clearTrajectory() {
    if (trajPolyline) {
        map.removeLayer(trajPolyline);
        trajPolyline = null;
    }
}

function drawFullTrajectory(points) {
    clearTrajectory();
    var pts = points.map(function(p) { return [p.lat, p.lng]; });
    trajPolyline = L.polyline(pts, {color: '#2563eb', weight: 4}).addTo(map);
    if (pts.length > 0) {
        map.fitBounds(trajPolyline.getBounds(), {padding: [40, 40]});
    }
}
