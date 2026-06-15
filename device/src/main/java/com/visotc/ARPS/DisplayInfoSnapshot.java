package com.visotc.ARPS;

final class DisplayInfoSnapshot {
    final int width;
    final int height;
    final int rotation;
    final int layerStack;

    DisplayInfoSnapshot(int width, int height, int rotation) {
        this(width, height, rotation, 0);
    }

    DisplayInfoSnapshot(int width, int height, int rotation, int layerStack) {
        this.width = width;
        this.height = height;
        this.rotation = rotation;
        this.layerStack = layerStack;
    }
}
