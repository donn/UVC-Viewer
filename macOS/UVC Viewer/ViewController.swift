//
//  ViewController.swift
//  UVC Viewer
//
//  Created by Donn on 2021-03-20.
//

import Cocoa
import AVFoundation
import Promises

class ViewController: NSViewController {
    var captureSession: AVCaptureSession?
    
    @IBOutlet weak var preview: NSView!
    
    func requestPermissions() -> Promise<(video: Bool, audio: Bool)> {
        
        return Promise<Bool> {
            (resolve, reject) in
            
            switch AVCaptureDevice.authorizationStatus(for: .video) {
            case .authorized: // The user has previously granted access to the camera.
                resolve(true)
                return
                
            case .notDetermined: // The user has not yet been asked for camera access.
                AVCaptureDevice.requestAccess(for: .video) { granted in
                    resolve(granted)
                    return
                }
            default:
                resolve(false)
            }
            
        }.then {
            video in
            
            return Promise<(video: Bool, audio: Bool)> {
                (resolve, reject) in
                
                switch AVCaptureDevice.authorizationStatus(for: .audio) {
                case .authorized: // The user has previously granted access to the camera.
                    resolve((video: video, audio: true))
                    return
                    
                case .notDetermined: // The user has not yet been asked for camera access.
                    AVCaptureDevice.requestAccess(for: .audio) { granted in
                        resolve((video: video, audio: granted))
                        return
                    }
                default:
                    resolve((video: video, audio: false))
                    return
                }
                
            }
        }
        
    }
    
    func setupVideo() {
        
        print("Setting up video…")
        
        let cs = captureSession!
        cs.sessionPreset = .low
        
        let devices = AVCaptureDevice.DiscoverySession(deviceTypes: [.externalUnknown], mediaType: .video, position: .unspecified).devices
        
        if devices.count == 0 {
            print("No video devices found.")
            return
        }
        
        let device = devices.filter { $0.localizedName.contains("Live") }[0]
        
        print("Using \(device)…")
        
        guard let input = try? AVCaptureDeviceInput(device: device), cs.canAddInput(input) else {
            print("Failed to add input.")
            return
        }
        
        cs.addInput(input)
        
        let previewLayer = AVCaptureVideoPreviewLayer(session: cs)
        previewLayer.frame = self.view.bounds
        
        
        self.preview.layer = previewLayer
        self.preview.wantsLayer = true
        
        cs.startRunning()
    }
    
    func setupAudio() {
        print("Setting up audio…")
        let cs = captureSession!
        
        let devices = AVCaptureDevice.DiscoverySession(deviceTypes: [.builtInMicrophone], mediaType: .audio, position: .unspecified).devices
        
        if devices.count == 0 {
            print("No audio devices found.")
            return
        }
        
        let device = devices.filter { $0.localizedName.contains("Live") }[0]
        
        print("Using \(device)…")
        
        
        guard let input = try? AVCaptureDeviceInput(device: device), cs.canAddInput(input) else {
            print("Failed to add input.")
            return
        }
        
        cs.addInput(input)
        
        let audioOutput = AVCaptureAudioPreviewOutput()
        
        audioOutput.volume = 0.5
        
        cs.addOutput(audioOutput)
        
    }
    
    override func viewDidLoad() {
        super.viewDidLoad()
        
        requestPermissions().then {
            av in
            
            self.captureSession = AVCaptureSession()
            if (av.video) {
                self.setupVideo()
            }
            if (av.audio) {
                self.setupAudio()
            }
                        
        }

        // Do any additional setup after loading the view.
    }

    override var representedObject: Any? {
        didSet {
        // Update the view, if already loaded.
        }
    }


}

