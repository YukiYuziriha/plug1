"""Font redistribution inventory shared by Windows release tools."""

OXANIUM_NOTICE = 'ThirdParty/Oxanium-OFL.txt'
OXANIUM_FONTS = ('source/assets/OxaniumMedium.ttf', 'source/assets/OxaniumSemiBold.ttf')


def oxanium_fonts(cmake):
    # Use the selected build source, not the release version or working-tree files.
    # A reference to either weight requires the complete font pair and notice.
    return OXANIUM_FONTS if any(name.removeprefix('source/') in cmake for name in OXANIUM_FONTS) else ()


def require_oxanium_files(cmake, has_file):
    fonts = oxanium_fonts(cmake)
    if fonts:
        missing = [name for name in (OXANIUM_NOTICE, *fonts) if not has_file(name)]
        if missing:
            raise RuntimeError('Missing Oxanium release inputs: ' + ', '.join(missing))
    return fonts
