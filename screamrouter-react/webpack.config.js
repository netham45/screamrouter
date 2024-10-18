import { resolve as _resolve, join } from 'path';
import { DefinePlugin } from 'webpack';
import HtmlWebpackPlugin from 'html-webpack-plugin';
import CopyWebpackPlugin from 'copy-webpack-plugin';

export const entry = './src/index.tsx';
export const output = {
  path: _resolve(__dirname, '../site'),
  filename: '[name].[contenthash].js',
  chunkFilename: '[name].[contenthash].js',
  publicPath: '/site/',
};
export const optimization = {
  splitChunks: {
    chunks: 'all',
  },
};
export const resolve = {
  extensions: ['.ts', '.tsx', '.js', '.jsx'],
};
export const module = {
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
  ],
};
export const plugins = [
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
  new DefinePlugin({
    'process.env': JSON.stringify(process.env)
  }),
];
export const devServer = {
  static: {
    directory: join(__dirname, '../site'),
  },
  compress: true,
  port: 8080,
  historyApiFallback: true,
  hot: true,
};