using System;

using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Controls.Primitives;
using Windows.UI.Xaml.Input;
using Windows.Media.Capture;
using Windows.System.Display;
using Windows.Graphics.Display;
using Windows.Devices.Enumeration;

using System.Threading.Tasks;
using System.Diagnostics;
using System.Collections.ObjectModel;
using Windows.Media.Audio;
using Windows.Media.Render;
using Windows.UI.Core;
using Windows.Storage;
using System.Threading;
using Windows.UI.ViewManagement;
using Windows.Media.MediaProperties;
using Windows.Media.Devices;

#pragma warning disable CS4014 // Because this call is not awaited, execution of the current method continues before the call is completed

namespace UVCViewer {
    public sealed partial class MainPage : Page {
        ApplicationDataContainer localSettings;
        bool refreshingDevices = false;

        MediaCapture mediaCapture;
        DisplayRequest displayRequest = new DisplayRequest();

        ObservableCollection<DeviceInformation> VideoDevices = new ObservableCollection<DeviceInformation>();

        ObservableCollection<DeviceInformation> AudioDevices = new ObservableCollection<DeviceInformation>();
        AudioGraph graph;
        AudioDeviceOutputNode deviceOutputNode;
        AudioDeviceInputNode deviceInputNode;

        private double gain {
            get {
                return VolumeSlider.Value / 100;
            }
        }
        private bool muted {
            get {
                return (AudioButton.Icon as SymbolIcon).Symbol == Symbol.Mute;
            }
        }
        private bool videoOff {
            get {
                return (VideoButton.Icon as SymbolIcon).Symbol == Symbol.UnFavorite;
            }
        }

        private void UpdateVolume() {
            if (deviceInputNode != null) {
                deviceInputNode.OutgoingGain = gain * (muted ? 0.0 : 1.0);
            }
        }


        // Initialization
        private async Task AsyncInitTasks() {
            VolumeSlider.Value = double.Parse(localSettings.Values["volume"] as string ?? "50");
            AudioButton.Icon = ((localSettings.Values["muted"] as string ?? "No") == "Yes") ? new SymbolIcon(Symbol.Mute) : new SymbolIcon(Symbol.Volume);
            VideoButton.Icon = ((localSettings.Values["videoOff"] as string ?? "No") == "Yes") ? new SymbolIcon(Symbol.UnFavorite) : new SymbolIcon(Symbol.SolidStar);
            DisplayInformation.AutoRotationPreferences = DisplayOrientations.Landscape;
            displayRequest.RequestActive();
            await RefreshDevices();
            await Preview();
        }
        public MainPage() {
            localSettings = ApplicationData.Current.LocalSettings;
            refreshingDevices = true;

            Window.Current.CoreWindow.KeyUp += OnKeyUp;
            Window.Current.VisibilityChanged += VisibilityChanged;
            MediaDevice.DefaultAudioRenderDeviceChanged += AudioOutputChanged;

            InitializeComponent();
            AsyncInitTasks();
        }

        // Helper Methods
        private static int findIndex<T>(ObservableCollection<T> collection, Func<T, bool> test) {
            int i;
            for (i = collection.Count - 1; i >= 0; i--) {
                if (test(collection[i])) {
                    break;
                }
            }
            return i;
        }
        private async Task<DeviceInformation> findDevice(ComboBox list, string name) {
            var item = list.SelectedItem;
            if (item == null) {
                throw new UserFacingException($"No {name} devices found.");
            }
            var info = item as DeviceInformation;
            Debug.WriteLine($"Using {name} device with id " + info.Id);
            return info;
        }

        // Loading
        private async Task LoadDeviceInfo(DeviceClass deviceClass, string name, ObservableCollection<DeviceInformation> collection, ComboBox sourceList) {
            var devices = await DeviceInformation.FindAllAsync(deviceClass);
            collection.Clear();
            Debug.WriteLine($"Found {devices.Count} {name} devices.");
            foreach (var device in devices) {
                Debug.WriteLine($"Name: {device.Name}, ID: {device.Id}");
                collection.Add(device);
            }
            var id = localSettings.Values[$"{name}Id"] as string;
            if (id != null) {
                Debug.WriteLine($"Found stored {name} device with ID {id}, attempting to use...");
                var index = findIndex(collection, device => device.Id == id);
                if (index != -1) {
                    sourceList.SelectedIndex = index;
                    Debug.WriteLine($"Attempt successful.");
                }
                else {
                    Debug.WriteLine($"Attempt failed: Could not find device.");
                }
            }
        }

        private async Task RefreshDevices() {
            refreshingDevices = true;
            await LoadDeviceInfo(DeviceClass.VideoCapture, "video", VideoDevices, VideoSourceList);
            await LoadDeviceInfo(DeviceClass.AudioCapture, "audio", AudioDevices, AudioSourceList);
            refreshingDevices = false;
        }


        private MediaCaptureInitializationSettings GetCaptureSettings(DeviceInformation inputDevice) {
            MediaCaptureInitializationSettings settings = new MediaCaptureInitializationSettings {
                StreamingCaptureMode = StreamingCaptureMode.Video,
                VideoDeviceId = inputDevice.Id
            };
            return settings;
        }

        private async Task CreateAudioGraph(DeviceInformation inputDevice) {

            AudioGraphSettings settings = new AudioGraphSettings(AudioRenderCategory.Media) {
                QuantumSizeSelectionMode = QuantumSizeSelectionMode.LowestLatency
            };

            CreateAudioGraphResult result = await AudioGraph.CreateAsync(settings);
            if (result.Status != AudioGraphCreationStatus.Success) {
                // Cannot create graph
                Debug.WriteLine($"Audio Graph creation failed: {result.Status}");
                return;
            }

            graph = result.Graph;

            CreateAudioDeviceOutputNodeResult deviceOutputNodeResult = await graph.CreateDeviceOutputNodeAsync();
            if (deviceOutputNodeResult.Status != AudioDeviceNodeCreationStatus.Success) {
                // Cannot create device output node
                Debug.WriteLine($"Audio Device Output unavailable: {deviceOutputNodeResult.Status}");
                return;
            }
            deviceOutputNode = deviceOutputNodeResult.DeviceOutputNode;

            CreateAudioDeviceInputNodeResult deviceInputNodeResult = await graph.CreateDeviceInputNodeAsync(MediaCategory.Other, null, inputDevice);
            if (deviceInputNodeResult.Status != AudioDeviceNodeCreationStatus.Success) {
                // Cannot create device input node
                Debug.WriteLine($"Audio Device Input unavailable: {deviceInputNodeResult.Status}");
                return;
            }
            deviceInputNode = deviceInputNodeResult.DeviceInputNode;
            deviceInputNode.AddOutgoingConnection(deviceOutputNode);
            UpdateVolume();

            graph.Start();

        }

        private async Task<string> UpdateVideo() {
            try {
                var videoInput = await findDevice(VideoSourceList, "video");
                try {
                    if (mediaCapture != null) {
                        PreviewControl.Source = null;
                        mediaCapture.Dispose();
                    }
                }
                catch (ObjectDisposedException) {
                    // Oh, cool then
                }
                mediaCapture = null;

                if (!videoOff) {
                    mediaCapture = new MediaCapture();
                    await mediaCapture.InitializeAsync(GetCaptureSettings(videoInput));
                    mediaCapture.VideoDeviceController.DesiredOptimization = MediaCaptureOptimization.LatencyThenPower;
                    PreviewControl.Source = mediaCapture;
                    await mediaCapture.StartPreviewAsync();
                }

                return null;

            }
            catch (UserFacingException e) {
                return e.Message;

            }
            catch (Exception e) {
                Debug.WriteLine("Video Exception: " + e.Message);
                return "Failed to load video.";
            }
        }

        private async Task<string> UpdateAudio() {
            try {
                var audioInput = await findDevice(AudioSourceList, "audio");
                try {
                    if (graph != null) {
                        graph.Dispose();
                    }
                }
                catch (ObjectDisposedException) {
                    // Oh, cool then
                }
                graph = null;

                await CreateAudioGraph(audioInput);

                return null;

            }
            catch (UserFacingException e) {
                return e.Message;
            }
            catch (Exception e) {
                Debug.WriteLine("Audio Exception: " + e.Message);
                return "Failed to load audio.";
            }
        }

        private async Task Preview() {
            var videoResult = await UpdateVideo();
            var audioResult = await UpdateAudio();
            if (audioResult != null && videoResult != null) {
                CommandBarMessage.Text = "Right-click the preview to show/hide this bar";
            }
            else {
                CommandBarMessage.Text = (videoResult ?? "") + " " + (audioResult ?? "");
            }
        }

        private void ToggleFullscreen() {
            var view = ApplicationView.GetForCurrentView();
            if (view.IsFullScreenMode) {
                FullScreenButton.Icon = new SymbolIcon(Symbol.FullScreen);
                view.ExitFullScreenMode();
                BottomBar.Visibility = Visibility.Visible;
            }
            else {
                if (view.TryEnterFullScreenMode()) {
                    FullScreenButton.Icon = new SymbolIcon(Symbol.Remove);
                }
                BottomBar.Visibility = Visibility.Collapsed;
            }
        }


        // External Stimuli
        private void AudioOutputChanged(object sender, DefaultAudioRenderDeviceChangedEventArgs args) {
            Dispatcher.RunAsync(CoreDispatcherPriority.Normal, () => {
                UpdateAudio();
            });
        }

        private void PreviewSizeChanged(object sender, SizeChangedEventArgs e) {
            PreviewControl.Height = e.NewSize.Height;
            PreviewControl.Width = e.NewSize.Width;
        }

        // UI Interactions
        private void VisibilityChanged(object sender, VisibilityChangedEventArgs e) {
            Debug.WriteLine($"Window {(e.Visible ? "visible" : "invisible")}.");
            if (e.Visible && PreviewControl.Source != null) {
                if (PreviewControl.Source.CameraStreamState != CameraStreamState.Streaming) {
                    Debug.WriteLine($"Detected that the Camera stream's state is {PreviewControl.Source.CameraStreamState}, restarting...");
                    UpdateVideo();
                }
            }
        }

        private void AVSelectionChanged(object sender, SelectionChangedEventArgs e) {
            if (refreshingDevices) {
                return;
            }
            var selectedVideoItem = VideoSourceList.SelectedItem;
            if (selectedVideoItem != null) {
                var videoInput = selectedVideoItem as DeviceInformation;
                localSettings.Values["videoId"] = videoInput.Id;
            }
            var selectedAudioItem = AudioSourceList.SelectedItem;
            if (selectedAudioItem != null) {
                var audioInput = selectedAudioItem as DeviceInformation;
                localSettings.Values["audioId"] = audioInput.Id;
            }
            Preview();

        }

        private void SliderChanged(object sender, RangeBaseValueChangedEventArgs e) {
            localSettings.Values["volume"] = $"{VolumeSlider.Value}";
            UpdateVolume();
        }

        // Taps and Buttons
        private void AudioRightTapped(object sender, RightTappedRoutedEventArgs e) {
            if (!muted) {
                AudioButton.Icon = new SymbolIcon(Symbol.Mute);
                localSettings.Values["muted"] = "Yes";
            }
            else {
                AudioButton.Icon = new SymbolIcon(Symbol.Volume);
                localSettings.Values["muted"] = "No";
            }
            UpdateVolume();
        }
        private void VideoRightTapped(object sender, RightTappedRoutedEventArgs e) {
            if (!videoOff) {
                VideoButton.Icon = new SymbolIcon(Symbol.UnFavorite);
                localSettings.Values["videoOff"] = "Yes";
            }
            else {
                VideoButton.Icon = new SymbolIcon(Symbol.SolidStar);
                localSettings.Values["videoOff"] = "No";
            }
            UpdateVideo();
        }
        private void PreviewRightTapped(object sender, RightTappedRoutedEventArgs e) {
            BottomBar.Visibility = (BottomBar.Visibility == Visibility.Visible) ? Visibility.Collapsed : Visibility.Visible;
        }

        private void PreviewDoubleTapped(object sender, DoubleTappedRoutedEventArgs e) {
            var properties = mediaCapture.VideoDeviceController.GetMediaStreamProperties(MediaStreamType.VideoPreview) as VideoEncodingProperties;
            var width = properties.Width;
            var height = properties.Height;
            Debug.WriteLine($"Attempting to resize to {width}x{height}...");
            ApplicationView.GetForCurrentView().TryResizeView(new Windows.Foundation.Size(properties.Width, properties.Height));
        }
        private void FullScreenTapped(object sender, TappedRoutedEventArgs e) {
            ToggleFullscreen();
        }

        private void RefreshDevicesTapped(object sender, TappedRoutedEventArgs e) {
            RefreshDevices();
        }
        private void ReloadPreviewTapped(object sender, TappedRoutedEventArgs e) {
            RefreshDevices();
            Preview();
        }

        private void OnKeyUp(CoreWindow sender, KeyEventArgs e) {
            if (e.VirtualKey == Windows.System.VirtualKey.Escape) {
                ToggleFullscreen();
            }
        }
    }
}
