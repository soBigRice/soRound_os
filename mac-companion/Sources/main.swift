import Foundation
import AppKit
import CoreBluetooth
import CoreText
import ImageIO

// 本地桥接只读取 Now Playing；控制仍由设备 HID 媒体键完成。
// This local bridge only reads Now Playing; device HID keys retain playback control.
let serviceUUID = CBUUID(string: "73520001-6e93-4fd8-bf40-2b54f4aa4f47")
let rxUUID = CBUUID(string: "73520002-6e93-4fd8-bf40-2b54f4aa4f47")
let statusUUID = CBUUID(string: "73520003-6e93-4fd8-bf40-2b54f4aa4f47")
let peripheralDiscoveryServices: [(String, CBUUID)] = [
    ("media", serviceUUID), ("battery", CBUUID(string: "180F")),
    ("device_info", CBUUID(string: "180A")), ("hid", CBUUID(string: "1812"))
]
let artworkBytes = 466 * 466 * 2
let overlayBytes = 350 * 180

func log(_ value: String) { FileHandle.standardError.write(Data((value + "\n").utf8)) }
func append32(_ number: UInt32, to data: inout Data) {
    for shift in stride(from: 0, to: 32, by: 8) { data.append(UInt8(truncatingIfNeeded: number >> shift)) }
}
func read32(_ data: Data, _ offset: Int) -> UInt32 {
    (0..<4).reduce(0) { $0 | UInt32(data[offset + $1]) << ($1 * 8) }
}
func packet(_ op: UInt8, generation: UInt32, offset: UInt32 = 0, value: UInt32 = 0, payload: Data = Data()) -> Data {
    var result = Data([0x53, 0x52, 1, op])
    append32(generation, to: &result); append32(offset, to: &result); append32(value, to: &result)
    result.append(payload); return result
}
func crc32(_ bytes: Data) -> UInt32 {
    var crc = UInt32.max
    for byte in bytes {
        crc ^= UInt32(byte)
        for _ in 0..<8 { crc = (crc >> 1) ^ ((0 &- (crc & 1)) & 0xedb88320) }
    }
    return crc ^ UInt32.max
}
func milliseconds(_ value: Double) -> UInt32 {
    guard value.isFinite, value > 0 else { return 0 }
    return UInt32(min(value * 1000, Double(UInt32.max)))
}

struct LyricLine { let seconds: Double; let text: String }
func parseLRC(_ source: String) -> [LyricLine] {
    let expression = try! NSRegularExpression(pattern: #"\[(\d+):(\d{1,2})(?:[.:](\d{1,3}))?\]"#)
    var result: [LyricLine] = []
    for line in source.components(separatedBy: .newlines) {
        let string = line as NSString
        let matches = expression.matches(in: line, range: NSRange(location: 0, length: string.length))
        guard let last = matches.last else { continue }
        let text = string.substring(from: last.range.location + last.range.length).trimmingCharacters(in: .whitespaces)
        for match in matches {
            guard let minute = Double(string.substring(with: match.range(at: 1))),
                  let second = Double(string.substring(with: match.range(at: 2))), second < 60 else { continue }
            let fractionRange = match.range(at: 3)
            let fraction = fractionRange.location == NSNotFound ? "" : string.substring(with: fractionRange)
            result.append(LyricLine(seconds: minute * 60 + second + (Double("0." + fraction) ?? 0), text: text))
        }
    }
    return result.sorted { $0.seconds < $1.seconds }
}
func lyricIndex(_ lines: [LyricLine], at time: Double) -> Int {
    lines.lastIndex { $0.seconds <= time } ?? -1
}

func drawLine(_ text: String, y: CGFloat, size: CGFloat, alpha: CGFloat, in context: CGContext) {
    guard !text.isEmpty else { return }
    let font = CTFontCreateWithName("PingFang SC" as CFString, size, nil)
    let attributes: [NSAttributedString.Key: Any] = [
        NSAttributedString.Key(kCTFontAttributeName as String): font,
        NSAttributedString.Key(kCTForegroundColorAttributeName as String): CGColor(gray: 1, alpha: alpha)
    ]
    let source = NSAttributedString(string: text, attributes: attributes)
    let line = CTLineCreateWithAttributedString(source)
    let token = CTLineCreateWithAttributedString(NSAttributedString(string: "…", attributes: attributes))
    let fitting = CTLineCreateTruncatedLine(line, 338, .end, token) ?? line
    let width = CGFloat(CTLineGetTypographicBounds(fitting, nil, nil, nil))
    context.textPosition = CGPoint(x: max(6, (350 - width) / 2), y: y)
    CTLineDraw(fitting, context)
}
func renderOverlay(title: String, artist: String, lyrics: [LyricLine], index: Int) -> Data? {
    var rgba = [UInt8](repeating: 0, count: 350 * 180 * 4)
    let valid = rgba.withUnsafeMutableBytes { raw -> Bool in
        guard let context = CGContext(data: raw.baseAddress, width: 350, height: 180, bitsPerComponent: 8,
                                      bytesPerRow: 350 * 4, space: CGColorSpaceCreateDeviceRGB(),
                                      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return false }
        context.setAllowsAntialiasing(true)
        // CoreText 完成字形回退及排版；LVGL 只显示 A8 遮罩。
        // CoreText shapes/falls back fonts; LVGL only displays the A8 mask.
        if lyrics.isEmpty {
            drawLine(title.isEmpty ? "等待系统媒体 / Waiting for media" : title, y: 101, size: 24, alpha: 1, in: context)
            drawLine(artist, y: 70, size: 15, alpha: 0.72, in: context)
            drawLine("未提供歌词 · No local lyrics", y: 36, size: 11, alpha: 0.4, in: context)
        } else {
            if index > 0 { drawLine(lyrics[index - 1].text, y: 142, size: 14, alpha: 0.35, in: context) }
            let current = index >= 0 ? lyrics[index].text : "♪"
            drawLine(current, y: 103, size: 24, alpha: 1, in: context)
            if index + 1 < lyrics.count { drawLine(lyrics[index + 1].text, y: 72, size: 14, alpha: 0.35, in: context) }
            drawLine(title, y: 29, size: 12, alpha: 0.8, in: context)
            drawLine(artist, y: 10, size: 11, alpha: 0.5, in: context)
        }
        return true
    }
    guard valid else { return nil }
    // CGContext 行序为上至下；直接输出 LVGL ALPHA_8 所需字节。
    // Bitmap row order is top-to-bottom, matching LVGL ALPHA_8 storage.
    return Data(stride(from: 3, to: rgba.count, by: 4).map { rgba[$0] })
}
func renderArtwork(_ source: Data) -> Data? {
    guard source.count <= 16 * 1024 * 1024,
          let imageSource = CGImageSourceCreateWithData(source as CFData, nil),
          let properties = CGImageSourceCopyPropertiesAtIndex(imageSource, 0, nil) as? [CFString: Any],
          let sourceWidth = properties[kCGImagePropertyPixelWidth] as? Int,
          let sourceHeight = properties[kCGImagePropertyPixelHeight] as? Int,
          sourceWidth > 0, sourceHeight > 0, sourceWidth <= 16384, sourceHeight <= 16384,
          let image = CGImageSourceCreateThumbnailAtIndex(imageSource, 0, [
              kCGImageSourceCreateThumbnailFromImageAlways: true,
              kCGImageSourceThumbnailMaxPixelSize: 466,
              kCGImageSourceShouldCacheImmediately: false
          ] as CFDictionary) else { return nil }
    var rgba = [UInt8](repeating: 0, count: 466 * 466 * 4)
    let valid = rgba.withUnsafeMutableBytes { raw -> Bool in
        guard let context = CGContext(data: raw.baseAddress, width: 466, height: 466, bitsPerComponent: 8,
                                      bytesPerRow: 466 * 4, space: CGColorSpaceCreateDeviceRGB(),
                                      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return false }
        context.setFillColor(CGColor(gray: 0, alpha: 1)); context.fill(CGRect(x: 0, y: 0, width: 466, height: 466))
        let scale = min(466 / Double(image.width), 466 / Double(image.height))
        let width = Double(image.width) * scale, height = Double(image.height) * scale
        context.interpolationQuality = .high
        context.draw(image, in: CGRect(x: (466 - width) / 2, y: (466 - height) / 2, width: width, height: height))
        return true
    }
    guard valid else { return nil }
    var output = Data(capacity: artworkBytes)
    for i in stride(from: 0, to: rgba.count, by: 4) {
        let rgb = (UInt16(rgba[i] >> 3) << 11) | (UInt16(rgba[i + 1] >> 2) << 5) | UInt16(rgba[i + 2] >> 3)
        output.append(UInt8(truncatingIfNeeded: rgb)); output.append(UInt8(truncatingIfNeeded: rgb >> 8))
    }
    return output
}

enum ArtworkUpdate {
    case keep
    case replace(source: Data, pixels: Data)
    case clear
}
func prepareArtwork(_ info: [String: Any], previous: Data?) -> ArtworkUpdate {
    // 元数据的进度更新不代表封面被撤回。/ A metadata progress update does not withdraw artwork.
    guard let encoded = info["artworkData"] as? String,
          let source = Data(base64Encoded: encoded) else { return .clear }
    if source == previous { return .keep }
    guard let pixels = renderArtwork(source) else { return .clear }
    return .replace(source: source, pixels: pixels)
}

final class LayerTransfer {
    let kind: UInt32, generation: UInt32, data: Data, crc: UInt32
    var offset = 0, begun = false
    var startedAt: Date?
    init(kind: UInt32, generation: UInt32, data: Data) {
        self.kind = kind; self.generation = generation; self.data = data; self.crc = crc32(data)
    }
}

struct PeripheralCandidate { let identifier: UUID; let name: String? }
func selectKnownPeripheralIDs(_ candidates: [PeripheralCandidate], requested: UUID?) -> [UUID] {
    var seen = Set<UUID>()
    return candidates.compactMap { candidate in
        let matches = requested.map { candidate.identifier == $0 }
            ?? (candidate.name?.caseInsensitiveCompare("soRound") == .orderedSame)
        guard matches, seen.insert(candidate.identifier).inserted else { return nil }
        return candidate.identifier
    }
}

struct MediaWriteDiagnostic {
    let opcode: UInt8, packetBytes: Int, offset: UInt32
    let withResponseMaximum: Int, withoutResponseMaximum: Int
    var summary: String {
        "opcode=\(opcode) offset=\(offset) packet_bytes=\(packetBytes) with_response_max=\(withResponseMaximum) without_response_max=\(withoutResponseMaximum)"
    }
}

final class MediaBridge: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var receiveCharacteristic: CBCharacteristic?, statusCharacteristic: CBCharacteristic?
    private var sourceProcess: Process?, sourceBuffer = Data(), timer: Timer?
    private var sourceInfo: [String: Any] = [:]
    private var controls: [Data] = [], pendingMeta: Data?
    private var cover: LayerTransfer?, overlay: LayerTransfer?
    private var inFlight = false, lastLayer: UInt32 = 1, generation: UInt32 = 0
    private var writeFinished: (() -> Void)?, writeStarted = Date.distantPast
    private var writeDiagnostic: MediaWriteDiagnostic?
    private var identity = "", title = "", artist = "", lastArtwork: Data?
    private var playing = false, duration = 0.0, elapsed = 0.0, timestamp = Date.distantPast
    private var lastLyric = Int.min, connected = false, stopped = false
    private var scanDeadline = Date.distantFuture
    private let requestedDevice: UUID?, lyrics: [LyricLine], lyricsTitle: String?
    private let resourceDirectory: URL
    private var lrcMatched = false

    init(device: UUID?, lyrics: [LyricLine], lyricsTitle: String?, resources: URL) {
        requestedDevice = device; self.lyrics = lyrics; self.lyricsTitle = lyricsTitle; resourceDirectory = resources
        super.init()
    }
    func start() throws {
        try startSource()
        central = CBCentralManager(delegate: self, queue: .main)
        scanDeadline = Date().addingTimeInterval(25)
        timer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in self?.heartbeat() }
    }
    private func startSource() throws {
        let task = Process(); task.executableURL = URL(fileURLWithPath: "/usr/bin/perl")
        task.arguments = [resourceDirectory.appendingPathComponent("mediaremote-adapter.pl").path,
                          resourceDirectory.appendingPathComponent("MediaRemoteAdapter.framework").path,
                          "stream", "--debounce=100"]
        let output = Pipe(); task.standardOutput = output
        // stderr 保留诊断；不在日志输出曲目或封面。/ Keep diagnostics without logging song/artwork payloads.
        task.standardError = FileHandle.standardError
        output.fileHandleForReading.readabilityHandler = { [weak self] handle in
            let data = handle.availableData
            guard !data.isEmpty else { handle.readabilityHandler = nil; return }
            DispatchQueue.main.async { self?.receiveSource(data) }
        }
        task.terminationHandler = { [weak self] process in
            DispatchQueue.main.async {
                guard let self, !self.stopped else { return }
                log("MediaRemote reader exited (\(process.terminationStatus)); bridge stopped. Restart manually after resolving the error.")
                self.stop(); exit(2)
            }
        }
        try task.run(); sourceProcess = task
    }
    private func receiveSource(_ bytes: Data) {
        sourceBuffer.append(bytes)
        guard sourceBuffer.count <= 24 * 1024 * 1024 else { log("Metadata payload exceeds local bound."); stop(); exit(2) }
        while let newline = sourceBuffer.firstIndex(of: 10) {
            let line = sourceBuffer.prefix(upTo: newline); sourceBuffer.removeSubrange(...newline)
            guard let event = try? JSONSerialization.jsonObject(with: line) as? [String: Any],
                  let payload = event["payload"] as? [String: Any] else { continue }
            if event["diff"] as? Bool != true { sourceInfo.removeAll() }
            for (key, value) in payload {
                if value is NSNull { sourceInfo.removeValue(forKey: key) } else { sourceInfo[key] = value }
            }
            applySource()
        }
    }
    private func applySource() {
        let newTitle = sourceInfo["title"] as? String ?? ""
        let newArtist = sourceInfo["artist"] as? String ?? ""
        let newIdentity = newTitle.isEmpty ? "" : [sourceInfo["bundleIdentifier"] as? String ?? "", sourceInfo["uniqueIdentifier"] as? String ?? "", newTitle, newArtist, sourceInfo["album"] as? String ?? ""].joined(separator: "\u{1f}")
        let changed = newIdentity != identity
        identity = newIdentity; title = newTitle; artist = newArtist
        playing = sourceInfo["playing"] as? Bool ?? false
        duration = (sourceInfo["duration"] as? NSNumber)?.doubleValue ?? 0
        elapsed = (sourceInfo["elapsedTime"] as? NSNumber)?.doubleValue ?? 0
        let rawStamp = (sourceInfo["timestamp"] as? NSNumber)?.doubleValue
        timestamp = rawStamp?.isFinite == true ? Date(timeIntervalSince1970: rawStamp!) : Date()
        if !elapsed.isFinite || elapsed < 0 { elapsed = 0 }
        if !duration.isFinite || duration < 0 { duration = 0 }
        if changed {
            generation &+= 1; if generation == 0 { generation = 1 }
            controls.removeAll(); pendingMeta = nil; cover = nil; overlay = nil; lastArtwork = nil; lastLyric = Int.min
            // LRC 需显式绑定标题，避免切歌后把上一首歌词当成当前歌词。
            // Explicit title binding prevents a local LRC from leaking into another track.
            lrcMatched = !lyrics.isEmpty && lyricsTitle?.caseInsensitiveCompare(title) == .orderedSame
            if connected { controls.append(metaPacket()) }
        }
        guard !identity.isEmpty else {
            if connected { controls = [packet(6, generation: 0)] }; pendingMeta = nil; cover = nil; overlay = nil
            pump(); return
        }
        switch prepareArtwork(sourceInfo, previous: lastArtwork) {
        case .keep:
            break
        case let .replace(source, pixels):
            lastArtwork = source
            cover = LayerTransfer(kind: 1, generation: generation, data: pixels)
        case .clear:
            let needsClear = lastArtwork != nil || cover != nil || changed
            lastArtwork = nil; cover = nil
            if connected && needsClear { controls.append(packet(5, generation: generation, offset: 1)) }
        }
        refreshOverlay(force: changed)
        pendingMeta = metaPacket(); pump()
    }
    private func currentElapsed() -> Double {
        let estimate = elapsed + (playing ? max(0, Date().timeIntervalSince(timestamp)) : 0)
        return duration > 0 ? min(duration, estimate) : estimate
    }
    private func metaPacket() -> Data {
        var payload = Data(); append32((playing ? 1 : 0) | (lrcMatched ? 2 : 0), to: &payload)
        append32(milliseconds(currentElapsed()), to: &payload); append32(milliseconds(duration), to: &payload)
        return packet(1, generation: generation, payload: payload)
    }
    private func refreshOverlay(force: Bool) {
        guard !identity.isEmpty else { return }
        let lines = lrcMatched ? lyrics : []
        let index = lyricIndex(lines, at: currentElapsed())
        guard force || index != lastLyric else { return }
        lastLyric = index
        if let mask = renderOverlay(title: title, artist: artist, lyrics: lines, index: index) {
            overlay = LayerTransfer(kind: 2, generation: generation, data: mask)
        }
    }
    private func heartbeat() {
        if !connected && Date() > scanDeadline { log("No ready soRound media service. Open Remote on the device, pair HID, then restart this tool."); stop(); exit(3) }
        if inFlight && Date().timeIntervalSince(writeStarted) > 5 {
            log("BLE write timed out; \(writeDiagnostic?.summary ?? "packet_context_unavailable"); stopping without replaying an old transfer."); stop(); exit(3)
        }
        guard connected, !identity.isEmpty else { return }
        pendingMeta = metaPacket(); refreshOverlay(force: false); pump()
    }
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        guard central.state == .poweredOn else {
            if central.state == .unauthorized { log("Bluetooth permission is required for the local device connection."); stop(); exit(3) }
            return
        }
        let retrieved: [CBPeripheral]
        if let requestedDevice { retrieved = central.retrievePeripherals(withIdentifiers: [requestedDevice]) }
        else {
            // macOS 的已连接 HID 外设可能仅通过 Battery/DeviceInfo 查询可见。
            // A system-connected HID peripheral can be visible through Battery/DeviceInfo queries only.
            retrieved = peripheralDiscoveryServices.flatMap { central.retrieveConnectedPeripherals(withServices: [$0.1]) }
        }
        let identifiers = selectKnownPeripheralIDs(retrieved.map { PeripheralCandidate(identifier: $0.identifier, name: $0.name) }, requested: requestedDevice)
        let known = identifiers.compactMap { id in retrieved.first { $0.identifier == id } }
        if known.count > 1 { log("More than one soRound connected. Select one using --device <Bluetooth UUID>."); stop(); exit(3) }
        if let first = known.first { connect(first) }
        else { central.scanForPeripherals(withServices: [CBUUID(string: "1812")], options: nil); log("Looking for the local soRound HID device…") }
    }
    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral, advertisementData: [String: Any], rssi RSSI: NSNumber) {
        guard self.peripheral == nil else { return }
        if let requestedDevice { guard requestedDevice == peripheral.identifier else { return } }
        else { guard peripheral.name == "soRound" else { return } }
        connect(peripheral)
    }
    private func connect(_ device: CBPeripheral) {
        peripheral = device; device.delegate = self; central.stopScan(); central.connect(device)
    }
    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) { peripheral.discoverServices([serviceUUID]) }
    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        log("Could not connect: \(error?.localizedDescription ?? "unknown error")"); stop(); exit(3)
    }
    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        guard !stopped else { return }; log("Device disconnected. Restart manually to reconnect."); stop(); exit(3)
    }
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard error == nil, let service = peripheral.services?.first(where: { $0.uuid == serviceUUID }) else {
            log("Media GATT service unavailable. Device firmware or cached pairing needs review."); stop(); exit(3)
        }
        peripheral.discoverCharacteristics([rxUUID, statusUUID], for: service)
    }
    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        receiveCharacteristic = service.characteristics?.first { $0.uuid == rxUUID }
        statusCharacteristic = service.characteristics?.first { $0.uuid == statusUUID }
        guard error == nil, receiveCharacteristic != nil, let statusCharacteristic else { log("Incomplete media service."); stop(); exit(3) }
        peripheral.readValue(for: statusCharacteristic)
    }
    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        guard characteristic.uuid == statusUUID, error == nil, let status = characteristic.value,
              status.count == 20, status[0] == 0x53, status[1] == 0x52, status[2] == 1 else {
            log("Media service rejected access. Keep Remote visible and complete encrypted HID pairing."); stop(); exit(3)
        }
        connected = true; controls = [packet(6, generation: 0)]
        log("Local encrypted media bridge ready. No cloud service; press Ctrl-C to stop.")
        lastArtwork = nil; lastLyric = Int.min
        if !identity.isEmpty { controls.append(metaPacket()); applySource() }
        pump()
    }
    private func pump() {
        guard !stopped, connected, !inFlight, let peripheral, let receiveCharacteristic else { return }
        var next: Data?
        var completion: (() -> Void)?
        if !controls.isEmpty { next = controls.removeFirst() }
        else if let meta = pendingMeta { next = meta; pendingMeta = nil }
        else {
            // 封面和歌词独立组包、逐片确认并交错发送。/ Independently ACK and interleave artwork/text.
            let transfer: LayerTransfer?
            if lastLayer == 1 { transfer = overlay ?? cover } else { transfer = cover ?? overlay }
            if let transfer {
                lastLayer = transfer.kind
                if !transfer.begun {
                    var checksum = Data(); append32(transfer.crc, to: &checksum)
                    next = packet(2, generation: transfer.generation, offset: transfer.kind, value: UInt32(transfer.data.count), payload: checksum)
                    completion = { transfer.begun = true; transfer.startedAt = Date() }
                } else if transfer.offset < transfer.data.count {
                    let limit = min(512, peripheral.maximumWriteValueLength(for: .withResponse)) - 16
                    guard limit > 0 else { log("BLE MTU too small."); stop(); exit(3) }
                    let count = min(limit, transfer.data.count - transfer.offset)
                    let payload = transfer.data.subdata(in: transfer.offset..<(transfer.offset + count))
                    next = packet(3, generation: transfer.generation, offset: UInt32(transfer.offset), value: transfer.kind, payload: payload)
                    completion = { transfer.offset += count }
                } else {
                    next = packet(4, generation: transfer.generation, offset: transfer.kind)
                    completion = { [weak self, weak transfer] in
                        guard let self, let transfer else { return }
                        if self.cover === transfer || self.overlay === transfer {
                            let seconds = Date().timeIntervalSince(transfer.startedAt ?? Date())
                            let layer = transfer.kind == 1 ? "artwork" : "overlay"
                            log("Media layer \(layer) committed: \(transfer.data.count) bytes, \(String(format: "%.2f", seconds)) seconds.")
                        }
                        if self.cover === transfer { self.cover = nil }
                        if self.overlay === transfer { self.overlay = nil }
                    }
                }
            }
        }
        guard let next else { return }
        // 仅保留传输边界，不记录歌曲、图像、设备标识或包负载。
        // Keep transport bounds only, never songs, images, device IDs or packet payloads.
        writeDiagnostic = MediaWriteDiagnostic(opcode: next[3], packetBytes: next.count, offset: read32(next, 8),
                                               withResponseMaximum: peripheral.maximumWriteValueLength(for: .withResponse),
                                               withoutResponseMaximum: peripheral.maximumWriteValueLength(for: .withoutResponse))
        inFlight = true; writeFinished = completion; writeStarted = Date()
        peripheral.writeValue(next, for: receiveCharacteristic, type: .withResponse)
    }
    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        guard characteristic.uuid == rxUUID else { return }
        inFlight = false
        let context = writeDiagnostic; writeDiagnostic = nil
        if let error {
            let failure = error as NSError
            log("Device rejected media packet: domain=\(failure.domain) code=\(failure.code) \(context?.summary ?? "packet_context_unavailable"). No automatic replay.")
            stop(); exit(3)
        }
        let completion = writeFinished; writeFinished = nil; completion?(); pump()
    }
    func stop() {
        guard !stopped else { return }; stopped = true
        timer?.invalidate(); timer = nil; central?.stopScan()
        if let peripheral { central?.cancelPeripheralConnection(peripheral) }
        controls.removeAll(); cover = nil; overlay = nil; pendingMeta = nil
        if let sourceProcess, sourceProcess.isRunning {
            sourceProcess.terminate()
            for _ in 0..<20 { if !sourceProcess.isRunning { break }; usleep(100_000) }
            if sourceProcess.isRunning { kill(sourceProcess.processIdentifier, SIGKILL) }
            sourceProcess.waitUntilExit()
        }
        sourceProcess = nil
    }
}

func selfTest(fixtures: String?) {
    precondition(crc32(Data("123456789".utf8)) == 0xcbf43926)
    let encoded = packet(3, generation: 7, offset: 123, value: 2, payload: Data([4, 5]))
    precondition(encoded.count == 18 && read32(encoded, 4) == 7 && read32(encoded, 8) == 123 && read32(encoded, 12) == 2)
    let lines = parseLRC("[00:01.25]你好\n[00:02.500][00:05]hello\n[01:90]invalid")
    precondition(lines.count == 3 && lines[0].seconds == 1.25 && lyricIndex(lines, at: 3) == 1)
    let mask = renderOverlay(title: "中文测试 / Test", artist: "本地歌手", lyrics: lines, index: 1)!
    precondition(mask.count == overlayBytes && mask.contains { $0 > 0 } && mask.contains { $0 == 0 })
    let empty = renderOverlay(title: "No lyrics", artist: "Offline", lyrics: [], index: -1)!
    precondition(empty.count == overlayBytes && empty != mask)
    let redBytes = Data([255, 0, 0, 255])
    let redProvider = CGDataProvider(data: redBytes as CFData)!
    let redImage = CGImage(width: 1, height: 1, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: 4,
                           space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
                           provider: redProvider, decode: nil, shouldInterpolate: false, intent: .defaultIntent)!
    let png = NSMutableData()
    let destination = CGImageDestinationCreateWithData(png, "public.png" as CFString, 1, nil)!
    CGImageDestinationAddImage(destination, redImage, nil); precondition(CGImageDestinationFinalize(destination))
    let converted = renderArtwork(png as Data)!
    precondition(converted.count == artworkBytes && converted[0] == 0 && converted[1] == 0xf8)
    precondition(renderArtwork(Data([1, 2, 3])) == nil)
    let imageBytes = png as Data
    let imageMetadata: [String: Any] = ["artworkData": imageBytes.base64EncodedString(), "elapsedTime": 42.0]
    for progress in [1.0, 42.0, 43.0] {
        var update = imageMetadata; update["elapsedTime"] = progress
        guard case .keep = prepareArtwork(update, previous: imageBytes) else {
            log("Mac self-test failed: an unchanged artwork on a progress update must neither clear nor retransmit."); exit(1)
        }
    }
    guard case let .replace(source, pixels) = prepareArtwork(imageMetadata, previous: nil),
          source == imageBytes, pixels == converted else {
        log("Mac self-test failed: a new track must publish its artwork even when the album image matches the previous track."); exit(1)
    }
    for metadata: [String: Any] in [[:], ["artworkData": "invalid-base64!"], ["artworkData": Data([1, 2, 3]).base64EncodedString()]] {
        guard case .clear = prepareArtwork(metadata, previous: imageBytes) else {
            log("Mac self-test failed: absent or invalid artwork must clear the previous image."); exit(1)
        }
    }
    let firstID = UUID(uuidString: "00000000-0000-0000-0000-000000000001")!
    let secondID = UUID(uuidString: "00000000-0000-0000-0000-000000000002")!
    let candidates = [PeripheralCandidate(identifier: firstID, name: "soRound"),
                      PeripheralCandidate(identifier: firstID, name: "soRound"),
                      PeripheralCandidate(identifier: secondID, name: "Another device")]
    precondition(selectKnownPeripheralIDs(candidates, requested: nil) == [firstID])
    precondition(selectKnownPeripheralIDs(candidates, requested: secondID) == [secondID])
    precondition(selectKnownPeripheralIDs([PeripheralCandidate(identifier: firstID, name: nil)], requested: firstID) == [firstID])
    precondition(selectKnownPeripheralIDs([PeripheralCandidate(identifier: secondID, name: "soRound")], requested: firstID).isEmpty)
    precondition(selectKnownPeripheralIDs(candidates + [PeripheralCandidate(identifier: secondID, name: "soRound")], requested: nil).count == 2)
    let diagnostic = MediaWriteDiagnostic(opcode: 3, packetBytes: 512, offset: 496,
                                          withResponseMaximum: 512, withoutResponseMaximum: 244)
    precondition(diagnostic.summary == "opcode=3 offset=496 packet_bytes=512 with_response_max=512 without_response_max=244")
    if let fixtures {
        let directory = URL(fileURLWithPath: fixtures)
        try! FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        let fixture = renderOverlay(title: "合成测试曲目 / Synthetic", artist: "CoreText test fixture", lyrics: [LyricLine(seconds: 0, text: "晚风吹过橘色的海"), LyricLine(seconds: 1, text: "把此刻慢慢收藏"), LyricLine(seconds: 2, text: "等下一束光亮起来")], index: 1)!
        try! fixture.write(to: directory.appendingPathComponent("synthetic-overlay.a8"))
        var cover = Data(capacity: artworkBytes)
        for y in 0..<466 { for x in 0..<466 {
            let r = UInt16((x * 31) / 465), g = UInt16((y * 63) / 465), b = UInt16(((x + y) * 31) / 930)
            let pixel = (r << 11) | (g << 5) | b
            cover.append(UInt8(truncatingIfNeeded: pixel)); cover.append(UInt8(truncatingIfNeeded: pixel >> 8))
        }}
        try! cover.write(to: directory.appendingPathComponent("synthetic-cover.rgb565"))
        print("Synthetic fixtures: \(directory.path)")
    }
    print("Mac self-test: packet encoding, CRC32, local LRC parsing/binding inputs, CoreText A8 and full RGB565 ImageIO rendering passed")
}

final class BluetoothInventory: NSObject, CBCentralManagerDelegate {
    private var central: CBCentralManager!
    func start() {
        // 仅查询系统已连接外设；不扫描、不连接、不输出设备标识。/ Query connected peripherals only; no scan, connection or device IDs.
        central = CBCentralManager(delegate: self, queue: .main)
        Timer.scheduledTimer(withTimeInterval: 12, repeats: false) { _ in
            log("Bluetooth inventory timed out awaiting poweredOn; no connections attempted."); exit(3)
        }
    }
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        if central.state == .unauthorized { log("Bluetooth inventory not authorized; no connections attempted."); exit(3) }
        guard central.state == .poweredOn else { return }
        var report: [String: Any] = [:], matches = Set<UUID>()
        for (label, service) in peripheralDiscoveryServices {
            let peripherals = central.retrieveConnectedPeripherals(withServices: [service])
            let known = peripherals.filter { $0.name?.caseInsensitiveCompare("soRound") == .orderedSame }
            matches.formUnion(known.map(\.identifier))
            report[label] = ["connected_count": peripherals.count, "soround_name_matches": known.count]
        }
        report["unique_soround_matches"] = matches.count
        let encoded = try! JSONSerialization.data(withJSONObject: report, options: [.sortedKeys])
        print(String(data: encoded, encoding: .utf8)!); exit(0)
    }
}

let args = Array(CommandLine.arguments.dropFirst())
if args.contains("--self-test") {
    let fixtureIndex = args.firstIndex(of: "--fixtures")
    selfTest(fixtures: fixtureIndex.flatMap { $0 + 1 < args.count ? args[$0 + 1] : nil }); exit(0)
}
if args == ["--diagnose-bluetooth"] {
    let inventory = BluetoothInventory(); inventory.start(); RunLoop.main.run(); exit(0)
}
if args.contains("--help") {
    print("soRoundMedia [--device UUID] [--lrc /absolute/song.lrc --lrc-title 'Exact song title']\nManual local-only reader; no playback commands, cloud calls, login item or auto-reconnect.\n--self-test runs synthetic protocol/CoreText checks without Bluetooth or media access.\n--diagnose-bluetooth only counts already connected devices by service; no scan or connection.")
    exit(0)
}
func argument(_ name: String) -> String? {
    guard let index = args.firstIndex(of: name), index + 1 < args.count else { return nil }; return args[index + 1]
}
let allowedArguments = Set(["--device", "--lrc", "--lrc-title"])
if args.count % 2 != 0 || stride(from: 0, to: args.count, by: 2).contains(where: { !allowedArguments.contains(args[$0]) }) {
    log("Invalid arguments; use --help."); exit(1)
}
let device = argument("--device").flatMap(UUID.init(uuidString:))
if argument("--device") != nil && device == nil { log("Invalid device UUID."); exit(1) }
var lyrics: [LyricLine] = []
if let path = argument("--lrc") {
    guard argument("--lrc-title") != nil, let source = try? String(contentsOfFile: path, encoding: .utf8) else {
        log("Local LRC requires a readable UTF-8 file and explicit --lrc-title binding."); exit(1)
    }
    lyrics = parseLRC(source)
    if lyrics.isEmpty { log("Local LRC has no valid timestamped lyrics."); exit(1) }
}
let resources = Bundle.main.resourceURL ?? URL(fileURLWithPath: CommandLine.arguments[0]).deletingLastPathComponent().deletingLastPathComponent().appendingPathComponent("Resources")
let bridge = MediaBridge(device: device, lyrics: lyrics, lyricsTitle: argument("--lrc-title"), resources: resources)
signal(SIGINT, SIG_IGN); signal(SIGTERM, SIG_IGN); signal(SIGHUP, SIG_IGN)
let interrupt = DispatchSource.makeSignalSource(signal: SIGINT, queue: .main)
let terminate = DispatchSource.makeSignalSource(signal: SIGTERM, queue: .main)
let terminalClosed = DispatchSource.makeSignalSource(signal: SIGHUP, queue: .main)
interrupt.setEventHandler { bridge.stop(); exit(0) }; terminate.setEventHandler { bridge.stop(); exit(0) }
terminalClosed.setEventHandler { bridge.stop(); exit(0) }
interrupt.resume(); terminate.resume(); terminalClosed.resume()
do { try bridge.start(); RunLoop.main.run() }
catch { log("Could not start local MediaRemote reader: \(error.localizedDescription)"); bridge.stop(); exit(2) }
