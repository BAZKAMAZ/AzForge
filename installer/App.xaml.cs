using System;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Threading;

namespace AzForgeSetup
{
    public partial class App : Application
    {
        protected override void OnStartup(StartupEventArgs e)
        {
            base.OnStartup(e);

            DispatcherUnhandledException += OnDispatcherUnhandledException;

            var options = StartupArgs.Parse(e.Args);
            var window = new MainWindow(options);
            MainWindow = window;
            window.Show();
        }

        private void OnDispatcherUnhandledException(object sender, DispatcherUnhandledExceptionEventArgs e)
        {
            // Swallow transient clipboard lock COMExceptions (CLIPBRD_E_CANT_OPEN) in WPF
            if (e.Exception is COMException com && unchecked((uint)com.ErrorCode) == 0x800401D0u)
            {
                e.Handled = true;
            }
        }
    }
}
