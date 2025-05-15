<?xml version="1.0" encoding="utf-8"?>
<xsl:stylesheet version="1.0"
    xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
    xmlns:wix="http://schemas.microsoft.com/wix/2006/wi">

    <xsl:output method="xml" indent="yes" omit-xml-declaration="yes"/>

    <!-- Identity template: copy everything not explicitly handled -->
    <xsl:template match="@*|node()">
        <xsl:copy>
            <xsl:apply-templates select="@*|node()"/>
        </xsl:copy>
    </xsl:template>

    <!-- Remove all Directory elements with Name="__pycache__" -->
    <xsl:template match="wix:Directory[@Name='__pycache__']"/>

    <!-- Remove all Component elements whose File child has a Source ending with .pyc inside a __pycache__ directory -->
    <xsl:template match="wix:Component[contains(wix:File/@Source, '__pycache__') and substring(wix:File/@Source, string-length(wix:File/@Source) - string-length('.pyc') + 1) = '.pyc']"/>

    <!-- Remove specific .dist-info files that might cause issues or are not needed -->
    <xsl:template match="wix:Component[wix:File/@Name='INSTALLER' and contains(wix:File/@Source, '.dist-info')]" />
    <xsl:template match="wix:Component[wix:File/@Name='RECORD' and contains(wix:File/@Source, '.dist-info')]" />
    <xsl:template match="wix:Component[wix:File/@Name='REQUESTED' and contains(wix:File/@Source, '.dist-info')]" />
    <!-- You might need to add more specific filters for other problematic .dist-info files if they appear -->

</xsl:stylesheet>
