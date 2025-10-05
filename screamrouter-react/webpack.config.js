const path = require('path');
const webpack = require('webpack');
const HtmlWebpackPlugin = require('html-webpack-plugin');
const CopyWebpackPlugin = require('copy-webpack-plugin');

module.exports = {
  entry: './src/index.tsx',
  output: {
    path: path.resolve(__dirname, '../site'),
    filename: '[name].[contenthash].js',
    chunkFilename: '[name].[contenthash].js',
    publicPath: '/site/',
  },
  optimization: {
    splitChunks: {
      chunks: 'all',
    },
  },
  resolve: {
    extensions: ['.ts', '.tsx', '.js', '.jsx'],
  },
  module: {
    rules: [
      {
        test: /\.(ts|tsx)$/,
        exclude: /node_modules/,
        use: {
          loader: 'babel-loader',
          options: {
            presets: [
              '@babel/preset-env',
              '@babel/preset-react',
              '@babel/preset-typescript'
            ]
          }
        },
      },
      {
        test: /\.css$/,
        use: ['style-loader', 'css-loader'],
      },
      {
        test: /\.(wav|mp3|ogg)$/,
        type: 'asset/resource',
        generator: {
          filename: 'static/media/[hash][ext][query]'
        }
      },
    ],
  },
  plugins: [
    new HtmlWebpackPlugin({
      template: './public/index_template.html',
      filename: 'index.html',
      inject: 'body',
      minify: {
        removeComments: true,
        collapseWhitespace: true,
        removeRedundantAttributes: true,
        useShortDoctype: true,
        removeEmptyAttributes: true,
        removeStyleLinkTypeAttributes: true,
        keepClosingSlash: true,
        minifyJS: true,
        minifyCSS: true,
        minifyURLs: true,
      },
    }),
    new CopyWebpackPlugin({
      patterns: [
        {
          from: 'public',
          to: '.',
          globOptions: {
            ignore: ['**/index.html', '**/index_template.html']
          }
        },
      ],
    }),
  ],
  devServer: {
    static: {
      directory: path.join(__dirname, '../site'),
    },
    compress: true,
    historyApiFallback: true,
    hot: true,
    client: {
      webSocketURL: 'auto://0.0.0.0/ws',
    }
  }
};
