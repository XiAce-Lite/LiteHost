#pragma once

/** While alive, suppress blocking UI that plugins may show during VST scans (MessageBox etc.). */
class ScanUiSuppressor
{
public:
    ScanUiSuppressor();
    ~ScanUiSuppressor();

    ScanUiSuppressor (const ScanUiSuppressor&) = delete;
    ScanUiSuppressor& operator= (const ScanUiSuppressor&) = delete;

private:
    struct Impl;
    Impl* impl = nullptr;
};
