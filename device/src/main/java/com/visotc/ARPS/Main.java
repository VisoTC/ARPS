package com.visotc.ARPS;

public final class Main {
    private Main() {
    }

    public static void main(String[] args) {
        int exitCode;
        ArpsClient client = null;
        try {
            Options options = Options.parse(args);
            client = new ArpsClient(options);
            ArpsClient shutdownClient = client;
            Runtime.getRuntime().addShutdownHook(new Thread(shutdownClient::shutdown,
                    "arps-shutdown"));
            exitCode = client.run();
        } catch (Throwable e) {
            Log.e("Fatal startup error", e);
            exitCode = 1;
            if (client != null) {
                client.shutdown();
            }
        }
        System.exit(exitCode);
    }
}
