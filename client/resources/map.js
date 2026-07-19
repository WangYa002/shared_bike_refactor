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
}

function renderBikes(bikesJson) {
    var bikes = JSON.parse(bikesJson);
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
    if (window.bridge && window.bridge.bikeCountsUpdated) {
        window.bridge.bikeCountsUpdated(idleCount, damagedCount);
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

function drawFullTrajectory(pointsJson) {
    clearTrajectory();
    var pts = JSON.parse(pointsJson).map(function(p) { return [p.lat, p.lng]; });
    trajPolyline = L.polyline(pts, {color: '#2563eb', weight: 4}).addTo(map);
    if (pts.length > 0) {
        map.fitBounds(trajPolyline.getBounds(), {padding: [40, 40]});
    }
}
