require "formula"

# Documentation: https://github.com/Homebrew/homebrew/wiki/Formula-Cookbook

class Knowthelist < Formula
  homepage "http://knowthelist.github.io/knowthelist"
  url "https://github.com/knowthelist/knowthelist/archive/v2.3.0.tar.gz"
  sha1 "e97a68784b7056c8e7cfb11e926215116688380d"
  head "https://github.com/knowthelist/knowthelist.git"

  depends_on "gstreamer"
  depends_on "gst-plugins-base"
  depends_on "gst-plugins-good"
  depends_on "gst-plugins-ugly" => "with-mad"
  depends_on "taglib"
  depends_on 'qt'

  def install
    system "qmake"
    system "make"
    bin.install buildpath/"knowthelist.app"
    system "cp -R #{bin}/knowthelist.app /Applications"
  end

  test do
    system "false"
  end
end
